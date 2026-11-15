# third_party

As dependências são obtidas pelo CMake (`FetchContent`) com **tag fixa** — nada é
vendorizado nem baixado na máquina do jogador.

| Biblioteca | Versão | Licença | Uso |
|---|---|---|---|
| [ENet](https://github.com/lsalzman/enet) | v1.3.18 | MIT | Transporte UDP (confiável/ordenado + não confiável) |
| [MinHook](https://github.com/TsudaKageyu/minhook) | v1.3.4 | BSD-2 | Inline hooks x86 (somente Windows) |
| ShadowHook *ou* Dobby | a fixar na Fase 1 | MIT / Apache-2.0 | Inline hooks ARMv7/ARM64 (somente Android) |

Para builds offline, aponte `FETCHCONTENT_SOURCE_DIR_ENET` / `FETCHCONTENT_SOURCE_DIR_MINHOOK`
para cópias locais.
