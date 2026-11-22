// Listening on Windows: through WASAPI, the sound system's own interface.
//
// Windows lets a recorder ask for the sound of one process and of everything
// that process started, or for all the sound but theirs. The first is how one
// application is heard without rerouting it, the second how "everything"
// covers every output at once. A device is recorded the plain way: an input as
// it is, an output by its loopback, which is what it is playing. In each case
// Windows converts to the format the analyzer takes. The demo track is played
// on the default output and heard as Trance's own process.
//
// A listening runs on a thread of its own, woken by Windows when there is
// sound, and hands what it gets to the thread the analyzer lives on.
#include "audiocapture.h"

#include <QElapsedTimer>
#include <QHash>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

#include <qt_windows.h>

#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <tlhelp32.h>
#include <wrl/client.h>
#include <wrl/implements.h>

#include "audio.h"

using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::FtmBase;
using Microsoft::WRL::Make;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

namespace {

// How much sound Windows keeps for a listening that is late to collect it,
// in its units of 100 ns: 200 ms. It adds no delay, since each 10 ms or so
// is collected as it arrives.
const REFERENCE_TIME Buffer = 2000000;

// Nothing arriving for this long, in ms, is silence. An output's loopback
// sends nothing at all while nothing plays, and the analyzer has to be told
// that it is quiet, or it would go on showing the last sound it was given.
const int Gap = 50;

// PKEY_Device_FriendlyName: a device's name as Windows shows it, as in
// "Speakers (Realtek(R) Audio)". Written out here because the header it is
// in only declares it unless asked, in the right order, to define it.
const PROPERTYKEY FriendlyName = {{0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

// COM, which all of this is reached through, for as long as this lives. A
// thread that already has it in the other of its two modes (Qt's own does)
// keeps that, and can use these interfaces just the same.
struct Com {
    const HRESULT entered = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~Com() {
        if (SUCCEEDED(entered))
            CoUninitialize();
    }
};

struct Handle {
    HANDLE handle = nullptr;
    Handle() = default;
    explicit Handle(HANDLE taken) : handle(taken) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() {
        if (handle)
            CloseHandle(handle);
    }
};

// What Windows calls whichever output is the default: a stream opened on it
// moves when another output becomes the default. DEVINTERFACE_AUDIO_RENDER,
// written out for the same reason as FriendlyName.
const wchar_t *const DefaultOutput = L"{E6327CAD-DCEC-4949-AE8A-991E976A79D2}";

// Told by Windows, on a thread of its choosing, that a device or the
// listening to a process that was asked for is ready to be picked up. What
// was asked for is kept here, not on the stack of the one asking: Windows
// holds on to this until it answers, and a listening told to stop doesn't
// wait for that.
class Activated : public RuntimeClass<RuntimeClassFlags<ClassicCom>, FtmBase, IActivateAudioInterfaceCompletionHandler> {
public:
    Activated() : done(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    Activated(DWORD process, bool only) : Activated() {
        asked = true;
        wanted.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        wanted.ProcessLoopbackParams.TargetProcessId = process;
        wanted.ProcessLoopbackParams.ProcessLoopbackMode =
            only ? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
        wrapped.vt = VT_BLOB;
        wrapped.blob.cbSize = sizeof(wanted);
        wrapped.blob.pBlobData = reinterpret_cast<BYTE *>(&wanted);
    }
    ~Activated() { CloseHandle(done); }
    STDMETHOD(ActivateCompleted)(IActivateAudioInterfaceAsyncOperation *) override {
        SetEvent(done);
        return S_OK;
    }
    PROPVARIANT *params() { return asked ? &wrapped : nullptr; }
    const HANDLE done;
    bool asked = false;
    AUDIOCLIENT_ACTIVATION_PARAMS wanted = {};
    PROPVARIANT wrapped = {};
};

// Told by Windows when another microphone becomes the default one, which is
// the one "microphone" means.
class DefaultInput : public RuntimeClass<RuntimeClassFlags<ClassicCom>, IMMNotificationClient> {
public:
    DefaultInput() : moved(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~DefaultInput() { CloseHandle(moved); }
    STDMETHOD(OnDefaultDeviceChanged)(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eCapture && role == eConsole)
            SetEvent(moved);
        return S_OK;
    }
    STDMETHOD(OnDeviceStateChanged)(LPCWSTR, DWORD) override { return S_OK; }
    STDMETHOD(OnDeviceAdded)(LPCWSTR) override { return S_OK; }
    STDMETHOD(OnDeviceRemoved)(LPCWSTR) override { return S_OK; }
    STDMETHOD(OnPropertyValueChanged)(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
    const HANDLE moved;
};

// Being told of the default microphone changing, for as long as this lives.
struct Following {
    explicit Following(IMMDeviceEnumerator *enumerator) : enumerator(enumerator), told(Make<DefaultInput>()) {
        if (told && FAILED(enumerator->RegisterEndpointNotificationCallback(told.Get())))
            told.Reset();
    }
    Following(const Following &) = delete;
    Following &operator=(const Following &) = delete;
    ~Following() {
        if (told)
            enumerator->UnregisterEndpointNotificationCallback(told.Get());
    }
    IMMDeviceEnumerator *const enumerator;
    ComPtr<DefaultInput> told;
};

// A running process: the one that started it, and its program's name
// without the folder or ".exe", which is what an application is called here.
struct Running {
    DWORD parent = 0;
    QString name;
};

QHash<DWORD, Running> processes() {
    QHash<DWORD, Running> all;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return all;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more; more = Process32NextW(snapshot, &entry)) {
        QString name = QString::fromWCharArray(entry.szExeFile).section('\\', -1);
        if (name.endsWith(".exe", Qt::CaseInsensitive))
            name.chop(4);
        all.insert(entry.th32ProcessID, {entry.th32ParentProcessID, name});
    }
    CloseHandle(snapshot);
    return all;
}

// Whether a session that is running has any sound in it right now, by the
// meter Windows keeps of each. Without a meter, running is taken for it.
bool audible(IAudioSessionControl *session) {
    ComPtr<IAudioMeterInformation> meter;
    float peak = 0;
    if (FAILED(session->QueryInterface(IID_PPV_ARGS(&meter))) || FAILED(meter->GetPeakValue(&peak)))
        return true;
    return peak > 0;
}

// The processes that have sound open on any output right now. Windows' own
// sounds are left out, and so is Trance. Those that can be heard right now
// also go into sounding, if given: not a paused player, whose sound is only
// open, nor one that keeps it running in silence, as a game or a muted tab
// may.
QList<DWORD> playing(IMMDeviceEnumerator *enumerator, QList<DWORD> *sounding = nullptr) {
    QList<DWORD> found;
    ComPtr<IMMDeviceCollection> outputs;
    UINT count = 0;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &outputs)) ||
        FAILED(outputs->GetCount(&count)))
        return found;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        ComPtr<IAudioSessionManager2> manager;
        ComPtr<IAudioSessionEnumerator> sessions;
        int sessionCount = 0;
        if (FAILED(outputs->Item(i, &device)) ||
            FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager)) ||
            FAILED(manager->GetSessionEnumerator(&sessions)) || FAILED(sessions->GetCount(&sessionCount)))
            continue;
        for (int j = 0; j < sessionCount; ++j) {
            ComPtr<IAudioSessionControl> session;
            ComPtr<IAudioSessionControl2> details;
            AudioSessionState state = AudioSessionStateExpired;
            DWORD process = 0;
            if (FAILED(sessions->GetSession(j, &session)) || FAILED(session.As(&details)))
                continue;
            // Expired is a session whose sound has been closed: what is left
            // of an application that played earlier.
            if (details->IsSystemSoundsSession() == S_OK || FAILED(session->GetState(&state)) ||
                state == AudioSessionStateExpired || FAILED(details->GetProcessId(&process)) || process == 0 ||
                process == GetCurrentProcessId())
                continue;
            // One process can have several, a paused one among them.
            if (!found.contains(process))
                found << process;
            if (sounding && state == AudioSessionStateActive && !sounding->contains(process) && audible(session.Get()))
                *sounding << process;
        }
    }
    return found;
}

// A browser or Spotify plays from a process started by another of the same
// program, and it is the first of them that has to be asked for to get them
// all. A process's number is handed out again once it is gone, so the way up
// is not followed without end.
DWORD topmost(const QHash<DWORD, Running> &all, DWORD process) {
    for (int climbed = 0; climbed < 32; ++climbed) {
        const Running self = all.value(process);
        const auto parent = all.constFind(self.parent);
        if (self.parent == process || parent == all.constEnd() ||
            parent->name.compare(self.name, Qt::CaseInsensitive) != 0)
            break;
        process = self.parent;
    }
    return process;
}

// The process to ask for to hear this application, or 0 if it isn't running.
// With onlySounding, one that can be heard right now other than besides, or
// 0 if there is none.
DWORD processOf(const QString &application, IMMDeviceEnumerator *enumerator, bool onlySounding = false,
                DWORD besides = 0) {
    const QHash<DWORD, Running> all = processes();
    const auto called = [&](DWORD process) {
        const auto found = all.constFind(process);
        return found != all.constEnd() && found->name.compare(application, Qt::CaseInsensitive) == 0;
    };
    DWORD any = 0;
    for (auto process = all.constBegin(); process != all.constEnd() && !any; ++process)
        if (called(process.key()))
            any = process.key();
    if (!any)
        return 0;
    // Started twice, it is the one playing that is meant, or else one with
    // sound open.
    QList<DWORD> sounding;
    const QList<DWORD> open = playing(enumerator, &sounding);
    for (const DWORD process : sounding)
        if (called(process) && topmost(all, process) != besides)
            return topmost(all, process);
    if (onlySounding)
        return 0;
    for (const DWORD process : open)
        if (called(process))
            return topmost(all, process);
    return topmost(all, any);
}

QString friendlyName(IMMDevice *device) {
    QString name;
    ComPtr<IPropertyStore> properties;
    PROPVARIANT value;
    PropVariantInit(&value);
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
        SUCCEEDED(properties->GetValue(FriendlyName, &value)) && value.vt == VT_LPWSTR)
        name = QString::fromWCharArray(value.pwszVal);
    PropVariantClear(&value);
    return name;
}

// What there is to listen to: the applications with sound open, and every
// output and input Windows knows of. A device is known by the id Windows
// gives it, which stays when the device is renamed. The ones that are
// unplugged, switched off or gone are among them, marked as such, so that a
// chosen one can still be called by its name.
bool ask(QList<AudioCapture::Node> *nodes) {
    using Node = AudioCapture::Node;
    const Com com;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
        return false;
    nodes->clear();

    const QHash<DWORD, Running> all = processes();
    for (const DWORD process : playing(enumerator.Get())) {
        Node node;
        node.kind = Node::Application;
        node.application = node.name = node.description = node.label = all.value(process).name;
        node.serial = process;
        if (!node.application.isEmpty())
            nodes->append(node);
    }

    for (const EDataFlow flow : {eRender, eCapture}) {
        ComPtr<IMMDeviceCollection> devices;
        UINT count = 0;
        if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATEMASK_ALL, &devices)) ||
            FAILED(devices->GetCount(&count)))
            continue;
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> device;
            LPWSTR id = nullptr;
            DWORD state = 0;
            if (FAILED(devices->Item(i, &device)) || FAILED(device->GetId(&id)))
                continue;
            Node node;
            node.kind = flow == eRender ? Node::Output : Node::Input;
            node.name = QString::fromWCharArray(id);
            CoTaskMemFree(id);
            node.available = SUCCEEDED(device->GetState(&state)) && state == DEVICE_STATE_ACTIVE;
            node.description = friendlyName(device.Get());
            if (node.description.isEmpty())
                node.description = node.name;
            node.label = node.description;
            nodes->append(node);
        }
    }
    return true;
}

// Asks Windows for a device by the name it is reached by. The answer comes
// later and elsewhere, and is waited for here, unless told to stop first.
HRESULT activate(const wchar_t *device, const ComPtr<Activated> &activated, HANDLE stop,
                 ComPtr<IAudioClient> *client) {
    if (!activated)
        return E_OUTOFMEMORY;
    ComPtr<IActivateAudioInterfaceAsyncOperation> operation;
    HRESULT hr = ActivateAudioInterfaceAsync(device, __uuidof(IAudioClient), activated.Get()->params(),
                                             activated.Get(), &operation);
    if (FAILED(hr))
        return hr;
    const HANDLE either[2] = {stop, activated.Get()->done};
    const DWORD woke = WaitForMultipleObjects(2, either, FALSE, 5000);
    if (woke != WAIT_OBJECT_0 + 1)
        return woke == WAIT_OBJECT_0 ? E_ABORT : HRESULT_FROM_WIN32(ERROR_TIMEOUT);
    HRESULT result = E_FAIL;
    ComPtr<IUnknown> made;
    hr = operation->GetActivateResult(&result, &made);
    if (SUCCEEDED(hr))
        hr = result;
    if (SUCCEEDED(hr))
        hr = made.As(client);
    return hr;
}

// The sound of one process and all it started, or of everything but those.
HRESULT processLoopback(DWORD process, bool only, HANDLE stop, ComPtr<IAudioClient> *client) {
    return activate(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, Make<Activated>(process, only), stop, client);
}

// Plays the demo track on the default output, round and round, until told
// to stop. Windows going wrong, or there being no output, is tried again in
// a while: what the listening hears meanwhile is silence, which says so.
void playDemo(const QByteArray &track, HANDLE stop) {
    const Com com;
    const UINT32 frames = UINT32(track.size() / 4);
    UINT32 at = 0;
    if (!frames)
        return;
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = 2;
    format.nSamplesPerSec = AudioCapture::DemoRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = 4;
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    do {
        ComPtr<IAudioClient> client;
        ComPtr<IAudioRenderClient> out;
        const Handle ready(CreateEventW(nullptr, FALSE, FALSE, nullptr));
        UINT32 size = 0;
        HRESULT hr = activate(DefaultOutput, Make<Activated>(), stop, &client);
        if (SUCCEEDED(hr))
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                    Buffer, 0, &format, nullptr);
        if (SUCCEEDED(hr))
            hr = client->SetEventHandle(ready.handle);
        if (SUCCEEDED(hr))
            hr = client->GetBufferSize(&size);
        if (SUCCEEDED(hr))
            hr = client->GetService(IID_PPV_ARGS(&out));
        // Filled up, then started, then topped up each time Windows has
        // played some of it.
        bool started = false;
        const HANDLE waiting[2] = {stop, ready.handle};
        while (SUCCEEDED(hr)) {
            UINT32 queued = 0;
            BYTE *data = nullptr;
            hr = client->GetCurrentPadding(&queued);
            const UINT32 room = size - queued;
            if (SUCCEEDED(hr) && room > 0 && SUCCEEDED(hr = out->GetBuffer(room, &data))) {
                for (UINT32 done = 0; done < room;) {
                    const UINT32 run = qMin(room - done, frames - at);
                    std::memcpy(data + 4 * size_t(done), track.constData() + 4 * size_t(at), 4 * size_t(run));
                    done += run;
                    at = (at + run) % frames;
                }
                hr = out->ReleaseBuffer(room, 0);
            }
            if (SUCCEEDED(hr) && !started) {
                hr = client->Start();
                started = SUCCEEDED(hr);
            }
            if (SUCCEEDED(hr) && WaitForMultipleObjects(2, waiting, FALSE, 1000) == WAIT_OBJECT_0)
                break;
        }
        if (started)
            client->Stop();
    } while (WaitForSingleObject(stop, 2000) != WAIT_OBJECT_0);
}

// Why the listening could not go on, to follow "Audio capture stopped:".
QString reason(HRESULT hr, const QString &source) {
    const bool gone = hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    if (gone && source == "microphone")
        return AudioCapture::tr("there is no microphone.");
    if (gone)
        return AudioCapture::tr("the device is not connected.");
    if (hr == E_ACCESSDENIED && (source == "microphone" || source.startsWith("input:")))
        return AudioCapture::tr("Windows is keeping applications from the microphone. See Privacy & security in "
                                "its settings.");
    if (hr == AUDCLNT_E_SERVICE_NOT_RUNNING)
        return AudioCapture::tr("the Windows Audio service is not running.");
    return AudioCapture::tr("Windows reported error 0x%1.").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

} // namespace

struct AudioCapture::Platform {
    // How a listening came to its end: told to, or by something going wrong
    // or away, which is tried again in a while. Or to be started again at
    // once: what its source means has become another (the default
    // microphone, or which of an application's copies is playing), or its
    // device was changed under it.
    enum Ended { Stopped, Lost, Again };

    // One listening's thread, and what tells it to stop.
    struct Listening {
        const Handle stop{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        std::thread thread;
        std::atomic<bool> finished{false};
    };

    explicit Platform(AudioCapture *capture) : q(capture) {}

    // Joins the stopped listenings that have finished, or with all, every
    // one, waiting for those that haven't.
    void reap(bool all);
    // Tells a listening or the player to stop, and leaves it to finish on
    // its own thread.
    void retire(std::unique_ptr<Listening> &thread);
    // Whether a listening that ended with Again is started again at once:
    // only a few times in a row. A device that keeps dropping its sound is
    // then said to have stopped, and tried again in a while, as one gone.
    bool again();
    Ended listen(const QString &source, int rate, quint64 mine, HANDLE stop, QString *why);

    AudioCapture *q;
    std::unique_ptr<Listening> listening;
    // The demo track's player, while it is what is listened to. Its thread
    // is the same kind as a listening's, and is stopped the same way, but
    // only when the listening is stopped from outside: through a listening
    // started again, it plays on.
    std::unique_ptr<Listening> playing;
    // Listenings told to stop, left to finish on their own threads. Windows
    // can take seconds to start a device, as a Bluetooth headset changing to
    // hands-free, and the window would freeze for as long if it were waited
    // for.
    std::vector<std::unique_ptr<Listening>> stopping;
    std::thread listing;
    // Which listening is the current one. What an earlier one sent before it
    // was stopped may still be on its way, and is told apart by this.
    quint64 current = 0;
    int againInARow = 0;
    QElapsedTimer sinceAgain;
};

bool AudioCapture::Platform::again() {
    againInARow = sinceAgain.isValid() && sinceAgain.elapsed() < 10000 ? againInARow + 1 : 1;
    sinceAgain.start();
    return againInARow <= 3;
}

void AudioCapture::Platform::retire(std::unique_ptr<Listening> &thread) {
    if (!thread)
        return;
    SetEvent(thread->stop.handle);
    stopping.push_back(std::move(thread));
}

void AudioCapture::Platform::reap(bool all) {
    for (auto it = stopping.begin(); it != stopping.end();) {
        if (all || (*it)->finished) {
            if ((*it)->thread.joinable())
                (*it)->thread.join();
            it = stopping.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioCapture::setUp() { m_platform = new Platform(this); }

void AudioCapture::tearDown() {
    if (m_platform->listing.joinable())
        m_platform->listing.join();
    m_platform->reap(true);
    delete m_platform;
    m_platform = nullptr;
}

void AudioCapture::open() {
    Platform *p = m_platform;
    p->reap(false);
    if (p->listening)
        return;
    // The player goes on through a listening that is started again.
    if (m_source == "demo" && !p->playing) {
        // Not tried again: what is built in doesn't mend.
        QByteArray track = demoTrack();
        if (track.isEmpty()) {
            setError(tr("The demo track could not be read."));
            return;
        }
        p->playing = std::make_unique<Platform::Listening>();
        Platform::Listening *player = p->playing.get();
        player->thread = std::thread([player, track = std::move(track)] {
            playDemo(track, player->stop.handle);
            player->finished = true;
        });
    }
    p->listening = std::make_unique<Platform::Listening>();
    Platform::Listening *own = p->listening.get();
    own->thread = std::thread([this, p, own, source = m_source, rate = m_analyzer->sampleRate(), mine = p->current] {
        QString why;
        const Platform::Ended ended = p->listen(source, rate, mine, own->stop.handle, &why);
        if (ended != Platform::Stopped)
            QMetaObject::invokeMethod(this, [this, p, mine, ended, why] {
                if (mine != p->current)
                    return;
                // Only the listening: the demo track plays on meanwhile, as
                // it does on Linux, rather than start over each time.
                ++p->current;
                p->retire(p->listening);
                if (ended == Platform::Again && p->again())
                    launch();
                else
                    lost(why);
            }, Qt::QueuedConnection);
        own->finished = true;
    });
}

void AudioCapture::close() {
    Platform *p = m_platform;
    ++p->current;
    p->retire(p->listening);
    p->retire(p->playing);
}

// One listening from start to end, on its own thread.
AudioCapture::Platform::Ended AudioCapture::Platform::listen(const QString &source, int rate, quint64 mine,
                                                             HANDLE stop, QString *why) {
    const Com com;
    const auto failed = [&](HRESULT hr) -> Ended {
        if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0)
            return Stopped;
        *why = reason(hr, source);
        return Lost;
    };

    ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
    if (FAILED(hr))
        return failed(hr);

    // The microphone is whichever input is the default one, so a change of
    // default is followed. A device chosen by name is kept to. Told of it
    // from before the default is asked for, so no change goes unheard.
    std::optional<Following> following;
    if (source == "microphone")
        following.emplace(enumerator.Get());

    ComPtr<IAudioClient> client;
    Handle application;
    const bool only = source.startsWith("app:");
    const bool demo = source == "demo";
    const QString applicationName = only ? source.mid(4) : QString();
    DWORD process = 0;
    // An application that can't be waited on, looked for instead.
    DWORD lookedFor = 0;
    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
    if (source == "system" || only || demo) {
        // Everything is all the sound but Trance's own, which is only ever
        // the demo track, heard as Trance's own and nothing else.
        process = GetCurrentProcessId();
        if (only) {
            process = processOf(applicationName, enumerator.Get());
            // Not running: lost() says that it is being waited for.
            if (!process)
                return Lost;
            // To notice it closing, which the sound alone would not tell:
            // it would only be quiet from then on.
            application.handle = OpenProcess(SYNCHRONIZE, FALSE, process);
            // Windows may refuse that, as for one run as administrator.
            // Then it is looked for every so often, or its closing would
            // go unnoticed and it would never be heard again.
            if (!application.handle)
                lookedFor = process;
        }
        hr = processLoopback(process, only || demo, stop, &client);
        flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    } else {
        ComPtr<IMMDevice> device;
        DWORD state = 0;
        if (source == "microphone")
            hr = enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device);
        else if (FAILED(enumerator->GetDevice(qUtf16Printable(source.mid(source.indexOf(':') + 1)), &device)))
            hr = AUDCLNT_E_DEVICE_INVALIDATED; // an id Windows doesn't know, or no longer does
        if (SUCCEEDED(hr))
            hr = device->GetState(&state);
        // Unplugged or switched off, Windows still knows it, but there is
        // nothing to hear from it.
        if (SUCCEEDED(hr) && state != DEVICE_STATE_ACTIVE)
            hr = AUDCLNT_E_DEVICE_INVALIDATED;
        if (SUCCEEDED(hr))
            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client);
        flags |= AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        if (source.startsWith("output:"))
            flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;
    }
    if (FAILED(hr))
        return failed(hr);

    // What the analyzer takes: 32-bit float stereo at its own rate.
    WAVEFORMATEX format = {};
    format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    format.nChannels = 2;
    format.nSamplesPerSec = DWORD(rate);
    format.wBitsPerSample = 32;
    format.nBlockAlign = WORD(2 * sizeof(float));
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

    const Handle ready(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    ComPtr<IAudioCaptureClient> packets;
    hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, Buffer, 0, &format, nullptr);
    if (SUCCEEDED(hr))
        hr = client->SetEventHandle(ready.handle);
    if (SUCCEEDED(hr))
        hr = client->GetService(IID_PPV_ARGS(&packets));
    if (SUCCEEDED(hr))
        hr = client->Start();
    if (FAILED(hr))
        return failed(hr);

    // Besides sound and being told to stop, one more thing can end it: the
    // application closing, or the default microphone changing.
    const bool moving = following && following->told;
    HANDLE waiting[3] = {stop, ready.handle, application.handle};
    if (moving)
        waiting[2] = following->told.Get()->moved;
    const DWORD count = waiting[2] ? 3 : 2;

    const auto deliver = [this, mine](std::vector<float> &samples) {
        QMetaObject::invokeMethod(q, [this, mine, heard = std::move(samples)] {
            if (mine == current)
                q->heard(heard.data(), int(heard.size() / 2));
        }, Qt::QueuedConnection);
        samples.clear();
    };

    // Ahead of ordinary work, as Windows offers to threads that handle sound.
    DWORD task = 0;
    const HANDLE priority = AvSetMmThreadCharacteristicsW(L"Audio", &task);
    QElapsedTimer quiet, sounded, sinceLooked, sinceAsked, running;
    quiet.start();
    sounded.start();
    sinceLooked.start();
    sinceAsked.start();
    running.start();
    // How often another copy of a quiet application is looked for: less
    // often the longer it stays quiet, as a paused player can for hours.
    qint64 askEvery = 2000;
    std::vector<float> samples;
    Ended ended = Stopped;
    for (;;) {
        const DWORD woke = WaitForMultipleObjects(count, waiting, FALSE, Gap);
        if (woke == WAIT_OBJECT_0)
            break;
        // A copy of the application closing may leave another playing,
        // which starting again finds, or says it is waiting for.
        if (woke == WAIT_OBJECT_0 + 2) {
            ended = moving || (only && running.elapsed() >= 1000) ? Again : Lost;
            break;
        }
        if (woke == WAIT_FAILED) {
            ended = failed(HRESULT_FROM_WIN32(GetLastError()));
            break;
        }
        // Collected when nothing was announced too: that is where a device
        // that was unplugged says so.
        UINT32 frames = 0;
        while (SUCCEEDED(hr = packets->GetNextPacketSize(&frames)) && frames > 0) {
            BYTE *data = nullptr;
            DWORD kind = 0;
            hr = packets->GetBuffer(&data, &frames, &kind, nullptr, nullptr);
            if (hr != S_OK)
                break;
            const size_t had = samples.size();
            samples.resize(had + size_t(frames) * 2);
            if (!(kind & AUDCLNT_BUFFERFLAGS_SILENT)) {
                float *const into = samples.data() + had;
                std::memcpy(into, data, size_t(frames) * 2 * sizeof(float));
                if (only && std::any_of(into, into + size_t(frames) * 2, [](float sample) { return sample != 0.f; }))
                    sounded.restart();
            }
            hr = packets->ReleaseBuffer(frames);
            if (FAILED(hr))
                break;
        }
        if (FAILED(hr)) {
            ended = failed(hr);
            // Changed under it rather than gone, as by a new sample rate, or
            // the default microphone unplugged before the new default is
            // announced: started again at once, which says so if it is gone.
            // Not when it had only just started, so as not to go round and
            // round.
            if (ended == Lost && hr == AUDCLNT_E_DEVICE_INVALIDATED && running.elapsed() >= 1000)
                ended = Again;
            break;
        }
        // Nothing for a while is silence, and is passed on as that much of
        // it. Not as all of a long wait, such as the computer's sleep.
        if (!samples.empty())
            quiet.restart();
        else if (quiet.elapsed() >= Gap)
            samples.assign(size_t(qMin<qint64>(quiet.restart(), 4 * Gap) * rate / 1000) * 2, 0.f);
        if (!samples.empty())
            deliver(samples);
        // Every so often, whether an application that can't be waited on is
        // still running, by its name too, as its number may have gone to
        // another program since.
        if (lookedFor && sinceLooked.elapsed() >= 2000) {
            sinceLooked.restart();
            const QHash<DWORD, Running> all = processes();
            const auto found = all.constFind(lookedFor);
            if (found == all.constEnd() || found->name.compare(applicationName, Qt::CaseInsensitive) != 0) {
                ended = Lost;
                break;
            }
        }
        // And once the application has been quiet a while, whether another
        // copy of it can be heard, to change to that.
        if (only && sounded.elapsed() < 3000)
            askEvery = 2000;
        else if (only && sinceAsked.elapsed() >= askEvery) {
            sinceAsked.restart();
            askEvery = qMin<qint64>(2 * askEvery, 10000);
            if (processOf(applicationName, enumerator.Get(), true, process)) {
                ended = Again;
                break;
            }
        }
    }
    if (priority)
        AvRevertMmThreadCharacteristics(priority);
    client->Stop();
    return ended;
}

void AudioCapture::refreshSources() {
    Platform *p = m_platform;
    // Asked on a thread of its own, as it can take a moment and the picture
    // is drawn on this one. One asking at a time.
    if (p->listing.joinable())
        return;
    p->listing = std::thread([this, p] {
        QList<Node> nodes;
        const bool asked = ask(&nodes);
        QMetaObject::invokeMethod(this, [this, p, nodes, asked] {
            p->listing.join();
            if (asked)
                listed(nodes);
        }, Qt::QueuedConnection);
    });
}

bool AudioCapture::listNodes(QList<Node> *nodes, QString *error) {
    if (ask(nodes))
        return true;
    *error = "Could not ask Windows what is playing.";
    return false;
}
