# GTA:SA Coop

[![build](../../actions/workflows/build.yml/badge.svg)](../../actions/workflows/build.yml)
![license](https://img.shields.io/badge/license-MIT-blue)
![platforms](https://img.shields.io/badge/platforms-Windows%20x86%20%7C%20Android%20ARMv7%2FARM64-informational)

Modo **cooperativo Online/LAN para até 4 jogadores** na campanha do *Grand Theft Auto: San Andreas*
clássico — PC (1.0 US) e Android (2.x), com cross-play e **sem CLEO, ModLoader, SA-MP, MTA ou
MoonLoader**.

> **Estado: pré-alfa (scaffold).** O repositório contém a arquitetura, o sistema de build, o CI e o
> núcleo de rede testado. Jogadores remotos, menu, veículos e missões estão no
> [roadmap](docs/ROADMAP.md) e ainda **não** são jogáveis.

| | |
|---|---|
| 📐 Arquitetura (SAD) | [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) |
| 🗺️ Milestones e issues | [docs/ROADMAP.md](docs/ROADMAP.md) |
| 📦 Dependências | [third_party/README.md](third_party/README.md) |

## Como funciona

- **PC** — um único arquivo, `dinput8.dll`, na pasta do jogo. O `gta_sa.exe` já importa `dinput8`;
  nossa DLL encaminha a chamada para a do sistema e instala os hooks. Nada do jogo é sobrescrito.
- **Android** — `libGTASA_Coop.so` é adicionada ao **seu** APK por um patcher e carregada pelo
  próprio jogo.
- **Rede** — ENet (UDP) em estrela: o host é o único que executa o `main.scm`; os clientes recebem
  entidades e comandos de script replicados. Sincronização por estado, sem dependência de
  determinismo entre x86 e ARM.

## Instalação (usuário final)

> Requer uma cópia legítima do jogo. Este projeto não distribui nenhum arquivo do GTA: San Andreas.

### Windows

1. Tenha o `gta_sa.exe` **1.0 US**. Em outras versões o coop se desativa sozinho e o jogo abre normal.
2. Baixe `gtasa-coop-<versão>-windows-x86.zip` em *Releases*.
3. Copie `dinput8.dll` para a pasta onde está o `gta_sa.exe`.
4. Abra o jogo. Para desinstalar, apague o arquivo.

No menu principal aparece **Cooperativo (1-4 Jogadores)**, logo abaixo de "Iniciar Jogo". Entrar por
ela abre a tela nativa de Novo Jogo/Carregar e, ao começar a partida, abre a sala como host
Quem se conecta aparece no mundo como um segundo CJ que acompanha a posição do outro jogador
(experimental: ainda sem animação, armas ou veículos — o personagem "desliza").

Entrar na sala de outra pessoa ainda é por parâmetro, assim como hospedar sem passar pelo menu:

```
gta_sa.exe -coop-host
gta_sa.exe -coop-connect 192.168.0.10
```

A DLL também aplica duas correções para Windows moderno, em qualquer versão do executável:

- **Resolução** — declara o jogo como *DPI-aware*, para que ele enxergue a resolução real do monitor
  quando a escala de tela do Windows está acima de 100 %.
- **Mouse** — readquire o mouse automaticamente quando o DirectInput o perde (alt-tab, troca de modo
  de vídeo), em vez de o mouse parar de responder.

Porta padrão: **7777/UDP**. O arquivo `coop.log` na pasta do jogo registra o que aconteceu.

### Linux (Wine / Proton)

Mesmo procedimento do Windows, mais a variável para o Wine preferir a DLL local:

```bash
WINEDLLOVERRIDES="dinput8=n,b" wine gta_sa.exe -coop-host
```

### Android

O patcher (`tools/apk-patcher`) faz parte do milestone M1. O fluxo previsto:

1. Faça backup dos saves e extraia o APK do jogo que você comprou.
2. Rode o patcher: ele adiciona a `.so`, a chamada de carregamento, e reassina o APK.
3. Desinstale o original (a assinatura muda) e instale o APK gerado.

## Compilação

Requisitos: CMake ≥ 3.21 e um compilador C++17. As dependências são baixadas pelo CMake.

```bash
# Núcleo + testes (Linux, macOS, Windows)
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

```powershell
# Windows x86 — Visual Studio 2022
cmake -S . -B build -A Win32
cmake --build build --config Release        # build\Release\dinput8.dll
```

```bash
# Windows x86 a partir do Linux — MinGW-w64 (pacote g++-mingw-w64-i686)
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-win                     # build-win/dinput8.dll
```

```bash
# Android — NDK r26+ (repita com -DANDROID_ABI=armeabi-v7a)
cmake -S . -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-24 -DANDROID_STL=c++_static \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android                 # build-android/libGTASA_Coop.so
```

O alvo Windows recusa builds x64 de propósito: o `gta_sa.exe` é 32 bits.

## CI e releases

Cada `push`/PR roda [build.yml](.github/workflows/build.yml): testes do núcleo, DLL x86 (MSVC e
MinGW) e `.so` para `armeabi-v7a` e `arm64-v8a`. Publicar uma *release* no GitHub anexa
automaticamente os pacotes de Windows e Android e o `SHA256SUMS.txt`.

## Estrutura

```
include/coop/      API pública do núcleo
src/core/          núcleo portável (rede, netcode, política de script) — sem headers do jogo
platform/win32/    dinput8.dll: proxy, hooks, endereços do 1.0 US
platform/android/  libGTASA_Coop.so: JNI_OnLoad, resolução por símbolo
tests/             testes do núcleo (rodam sem o jogo)
docs/              arquitetura e roadmap
```

## Contribuindo

- Escolha uma issue do milestone ativo em [docs/ROADMAP.md](docs/ROADMAP.md).
- `src/core` não pode incluir headers do jogo nem do sistema operacional; tudo que é específico vai
  em `platform/`.
- Endereços e offsets só em `platform/win32/Addresses.h` e `platform/android/Layout_*.h`, sempre com
  a fonte (gta-reversed/plugin-sdk) na descrição do PR.
- Mudou o formato de fio? Incremente `kProtocolVersion` e adicione um teste em `tests/`.

## Aviso legal

Projeto de fãs, sem vínculo com a Rockstar Games ou a Take-Two Interactive. *Grand Theft Auto* e
*San Andreas* são marcas de seus respectivos donos. Nenhum código, asset ou binário do jogo é
distribuído aqui. Licença do código deste repositório: [MIT](LICENSE).
