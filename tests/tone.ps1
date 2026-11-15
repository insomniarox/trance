# Plays a tone that comes and goes, four seconds of each, until it is stopped:
# something for Trance to hear, and to hear stop, where the tests are run with
# a sound card. See captureHearsWhatPlays in trance_tests.cpp.
$rate = 48000
$frames = $rate * 4
$wave = New-Object System.IO.MemoryStream
$out = New-Object System.IO.BinaryWriter $wave
$ascii = [Text.Encoding]::ASCII
$out.Write($ascii.GetBytes('RIFF')); $out.Write([int](36 + $frames * 2))
$out.Write($ascii.GetBytes('WAVEfmt ')); $out.Write([int]16)
$out.Write([int16]1); $out.Write([int16]1); $out.Write([int]$rate); $out.Write([int]($rate * 2))
$out.Write([int16]2); $out.Write([int16]16)
$out.Write($ascii.GetBytes('data')); $out.Write([int]($frames * 2))
for ($i = 0; $i -lt $frames; $i++) {
    $out.Write([int16](12000 * [Math]::Sin(2 * [Math]::PI * 440 * $i / $rate)))
}
$out.Flush()
$wave.Position = 0
$player = New-Object System.Media.SoundPlayer $wave
while ($true) {
    $player.PlaySync()
    Start-Sleep -Seconds 4
}
