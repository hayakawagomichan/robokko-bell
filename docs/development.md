# 開発用メモ

[← トップ](../README.md)

## 確認済み構成

| 項目 | 値 |
| --- | --- |
| 本体 | ATOM Voice（旧ATOM Echo）／ESP32-PICO-D4 |
| Arduino-ESP32 | 3.3.8 |
| Adafruit NeoPixel | 1.15.4 |
| FQBN | `esp32:esp32:pico32` |
| 書き込み速度・シリアルモニタ | 115200 |
| 音声 | 16kHz・16bit。マイクPDM入力とスピーカーI2S出力を切り替え |

## Arduino CLIで準備

[Arduino CLI](https://arduino.github.io/arduino-cli/)をインストールした上で実行します。

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.8 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "Adafruit NeoPixel@1.15.4"
```

リポジトリのルートで実行します。

```sh
arduino-cli compile --fqbn esp32:esp32:pico32 --build-path build AtomChristmasBell
arduino-cli board list
```

書き込みは接続先を確認してから行います。次の `COM3` は例なので、手元のポートに置き換えます。

```sh
arduino-cli upload --fqbn esp32:esp32:pico32 --port COM3 --input-dir build --upload-property upload.speed=115200 AtomChristmasBell
```

WindowsのPowerShellでは `./build.ps1` でコンパイル、`./build.ps1 -Upload -Port COM3` でコンパイル後に書き込めます。スクリプトは依存関係を自動インストールしません。

## 音の見本を作り直す

Node.jsを入れたPCで実行します。

```sh
node tools/make-preview.cjs
```

スケッチの音量・音符・拍数を読み、`docs/audio/` にWAVを出力します。
合成式やテンポの定数自体を変えた場合は、プレビュースクリプトも合わせて変更してください。本体への書き込みにNode.jsやWAVは不要です。

## 動作確認

2026年11月2日、実機で以下を確認しました。

- コンパイルと書き込み後のデータ照合が成功。
- マイク入力値が周囲の音で変化。
- ボタン長押しでベルを試聴。
- 拍手2回でベルを再生。
- 音量を `0.35` に上げた版で、ベルとジングルベルの両方を再生。

会場による誤検知や検出距離は、実際に使う環境で確認します。

## 参考資料

- [M5Stack：ATOM Voiceの端子・仕様](https://docs.m5stack.com/en/atom/Atom_Voice)
- [M5Stack：マイク／スピーカー切替の公式サンプル](https://github.com/m5stack/M5-ProductExampleCodes/blob/master/Core/Atom/AtomEcho/Arduino/Repeater/Repeater.ino)
- [Espressif：Arduino-ESP32の導入](https://docs.espressif.com/projects/arduino-esp32/en/latest/installing.html)
- [Adafruit NeoPixel](https://github.com/adafruit/Adafruit_NeoPixel)

曲は「Jingle Bells」の短いメロディーを音符として記述し、音色は数式で合成しています。市販の音源や歌詞は同梱していません。
