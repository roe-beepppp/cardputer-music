# CardPlay

Player de musica + web radio para o **M5Stack Cardputer ADV** (ESP32-S3FN8, sem PSRAM).
Tres UIs selecionaveis no aparelho (tecla `V`, fica salva): **Terminal** (A),
**Now Playing** (B) e **Tape Deck** (C) — mockups em `mockups/`. Modo **Radio** (tecla `R`).

## Instalar via Launcher (bmorcelli/Launcher)

1. Todo o acervo de musicas vai para o cartao SD (FAT32, MBR/SDHC), na raiz ou em pastas.
2. Copie `firmware.bin` (build local em `.pio/build/cardputer-adv/`, ou baixe o
   `.bin` da versao desejada em `dist/` / nos Releases do GitHub) para o cartao SD
   (sugestao: `CardPlay.bin`).
3. No Cardputer com o Launcher: **SD > CardPlay.bin > Install**. O Launcher cria a
   particao de app adequada e o app aparece no bootscreen/menu.
4. Tecla do Launcher que voce usa para abrir fica a seu criterio; o app nao precisa
   de bootloader/partition table proprios.

## Teclas

| Tela | Tecla | Acao |
|---|---|---|
| Lista | `;` / `.` | navega para cima / baixo |
| Lista | `,` / `/` | pagina acima / abaixo |
| Lista | `ENTER` ou `SPACE` | entra na pasta / toca (pasta inteira vira fila) |
| Lista | `DEL` | sobe um nivel (na raiz fecha a lista) |
| Lista | `B` / `TAB` | fecha a lista |
| Tocando | `SPACE` | pausa / continua |
| Tocando | `,` / `/` | faixa anterior (reinicia se >3s) / proxima |
| Tocando | `[` / `]` | retrocede / avanca 5s |
| Tocando | `;` / `.` | volume + / - (0..21, OSD na tela) |
| Tocando | `B` / `TAB` / `ENTER` | abre a lista |
| Qualquer | `DEL` | abre a lista (se fechada) |
| Qualquer | `V` | cicla a UI: Terminal / Now Playing / Tape Deck |
| Qualquer | `R` | liga/desliga o **modo Radio** (web radio) |
| Radio | `,` / `/` | estacao anterior / proxima |
| Radio | `B` / `TAB` | abre/fecha a lista de estacoes (`ENTER` toca) |
| Radio | `;` / `.` | volume + / - |
| Radio | `SPACE` | pausa (desconecta) / continua |
| Radio | `DEL` / `R` | sai do modo radio |

Toca pastas inteiras em sequencia (ordem natural, 01 < 02 < 10), com auto-next no fim.
Ultima pasta, volume, tema e ultima estacao ficam salvos na NVS.

## Radio (web radio)

- `/sd/wifi.txt`: **linha 1 = SSID, linha 2 = senha** (rede 2.4 GHz).
- `/sd/radio.txt`: uma estacao por linha — formatos aceitos: `Nome|URL`,
  `Nome,URL`, URL sozinha, `#EXTINF:-1,Nome` (M3U), linhas `#` = comentario.
- Ao entrar no radio o WiFi liga (STA); ao sair, o WiFi e desligado para liberar
  memoria (o app nao tem PSRAM). Streaming MP3/AAC/OGG via http/https.
- Nome da estacao e musica atual (ICY) aparecem na tela dos 3 temas.

## Qualidade de audio (bit-perfect ate o DAC)

- Fonte e lossy 16-bit (acervo: M4A/AAC-LC ~320k, MP3 320k, 1 FLAC) -> o player
  decodifica **bit-exact** (ESP32-audioI2S) e envia I2S **16-bit na sample rate nativa**
  (44.1k/48k), **sem resample**.
- O ES8311 roda com **volume de hardware em 0 dB (0xBF)** na posicao maxima; o volume do
  usuario e aplicado no proprio DAC (reg 0x32, passos de 2 dB, sem tocar nos bits de PCM).
- Downmix mono do codec e feito pelo hardware do ES8311. Mute do speaker com fone
  inserido e' hardware (jack 3.5mm).
- Amostra "ao vivo" do formato na tela: codec, sample rate e bitrate reais do arquivo.

## Formatos

MP3, M4A/AAC-LC, **FLAC** (decoder proprio `dr_flac` com arena estatica — ver abaixo),
WAV, OGG/Opus (decoders do ESP32-audioI2S).
> Obs.: sem PSRAM o lib e' pinado no tag **3.0.12** (unica linha com buffer de
> decodificacao em SRAM interna; >=3.3.x exige PSRAM) com patches locais em `lib/`.
> FLAC usa caminho dedicado (`src/flac_player.*` + `src/dr_flac.h`) porque o
> decoder da lib exige PSRAM; o dr_flac aloca numa arena estatica de 40KB, imune
> a fragmentacao de heap, e sai no mesmo I2S proprio (bit-perfect igual).

## Build

```sh
pio run                     # gera .pio/build/cardputer-adv/firmware.bin
```

Stack: platform pioarduino 55.03.312-1 (ESP-IDF 5.5), M5Unified 0.2.26,
M5Cardputer 1.2.0, ESP32-audioI2S 3.0.12. Board `m5stack-stamps3`.

### Pinagem (Cardputer ADV)

- I2S -> ES8311: BCK 41, LRCK 43, DOUT 42 (sem MCLK; codec escravo do BCLK)
- ES8311: I2C interno (SDA 8 / SCL 9), addr 0x18
- SD (SPI): CS 12, MOSI 14, SCK 40, MISO 39 (10 MHz, fallback 5/2.5/1.25 MHz)

## Estrutura

```
src/config.h      pinos, constantes, paletas dos 3 temas
src/es8311.*      init e volume HW do codec
src/audio_out.*   canal I2S proprio (16-bit, sample rate nativa)
src/player.*      fila, decode (task dedicada), metadados, volume/seek, NVS
src/flac_player.* decoder FLAC proprio (dr_flac + arena estatica)
src/dr_flac.h     dr_flac v0.13.4 vendorizado (single header)
src/browser.*     navegacao de pastas no SD (ordem natural, UTF-8)
src/radio.*       web radio: wifi.txt, parser de radio.txt, estado do WiFi
src/ui.*          UIs Terminal/Now Playing/Tape Deck + lista + OSD (canvas 240x135)
src/main.cpp      setup/loop, teclado, mount do SD
```

## Licenca

GPL-3.0 (ver `LICENSE`). Bibliotecas embutidas com licencas proprias:
ESP32-audioI2S (GPL-3.0, vendorizada com patches em `lib/`), dr_flac (public
domain / MIT-0, em `src/dr_flac.h`), M5Unified e M5Cardputer (MIT).
