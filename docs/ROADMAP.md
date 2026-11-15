# Roadmap — Milestones e Issues

Cada seção abaixo é um **GitHub Milestone**; cada item é uma **issue** (título pronto, rótulos entre
colchetes). Os critérios de aceite fecham o milestone. Arquitetura de referência:
[ARCHITECTURE.md](ARCHITECTURE.md).

Rótulos: `core` `win32` `android` `net` `ui` `script` `tools` `ci` `docs`

Estado atual do repositório (scaffold): build unificado, CI, proxy `dinput8` com hook de
`CGame::Process`, transporte ENet, formato de fio, buffer de interpolação, tabela-semente de opcodes,
`HandleMap`, testes do núcleo.

---

## M1 — MVP de movimentação

Objetivo: 2–4 jogadores se veem e se movem a pé no mundo livre. Sessão por linha de comando
(`-coop-host` / `-coop-connect <ip>`).

- [ ] `[core][net]` Handshake `Hello`/`Welcome`/`Reject` com atribuição de slot e checagem de protocolo
- [ ] `[core][net]` `TimeSync`: relógio de sessão com filtro de RTT mínimo e *slew*
- [ ] `[core]` `platform/common/IGame.h`: interface núcleo ↔ jogo (ler/aplicar estado de ped, criar/remover ped)
- [ ] `[win32]` Conferir todos os endereços de `Addresses.h` contra o gta-reversed e adicionar `static_assert` de layout
- [ ] `[win32]` Realocar `CWorld::Players` para 4 slots (lista de xrefs validada no boot)
- [ ] `[win32]` Criar/destruir `CPlayerPed` remoto por slot
- [ ] `[win32]` Troca de contexto em `CPlayerPed::ProcessControl` (pad sintetizado, `PlayerInFocus`, mira)
- [ ] `[core]` `PlayerSync`: bitmask lógico de teclas, correção exponencial, limiar de teleporte
- [ ] `[win32]` Suprimir HUD/câmera/áudio/wanted para slots remotos
- [x] `[android][tools]` `apk-patcher`: injeta `.so`, linha de `loadLibrary`, zipalign, assinatura — aguardando validação com APK real
- [x] `[android]` Tique por quadro via gancho de importação (`glClear`/`eglSwapBuffers`), sessão por `coop.txt`, posição local por símbolos exportados — aguardando validação em aparelho
- [ ] `[android]` Backend de inline hook (ShadowHook ou Dobby) atrás de `Hook.h`, para funções internas do jogo
- [ ] `[android]` `Layout_armv7.h` / `Layout_arm64.h` + verificações de sanidade no boot
- [ ] `[android]` Paridade com as issues `[win32]` acima (jogadores remotos, troca de contexto, toque → teclas lógicas)
- [ ] `[core]` Forçar limitador a 30 FPS durante a sessão
- [ ] `[ci]` Teste de integração host+2 clientes em loopback com perda/jitter simulados

**Aceite:** 4 jogadores (≥ 1 Android) andam, correm, pulam e agacham por 30 min sem crash; erro de
posição percebido < 0,5 m com 100 ms de RTT e 2 % de perda.

## M2 — Menu e lobby

Objetivo: entrar em sessão só pela interface nativa.

- [x] `[win32][ui]` Entrada "Cooperativo (1-4 Jogadores)" no menu principal (`aScreens`) — abre Novo Jogo/Carregar e hospeda; aguardando validação em jogo
- [ ] `[win32][ui]` Hook de `CText::Get` para chaves `COOP_*` (pt-BR, en, es) — hoje só `COOP_MN`, texto fixo
- [ ] `[win32][ui]` Páginas virtuais (id ≥ 44): hooks de `DrawStandardMenus`, `UserInput`, `SwitchToNewScreen`
- [ ] `[win32][ui]` Auditoria de todos os acessos a `aScreens[m_nCurrentMenuPage]` (guardas para páginas virtuais)
- [ ] `[win32][ui]` Campo de texto (IP / código) via `WM_CHAR`, com colar
- [ ] `[android][ui]` Item no `MainMenuScreen` + telas do coop com rotinas nativas
- [ ] `[android][ui]` Seletor de código por rodas; teclado do sistema para IP
- [ ] `[core][ui]` `LobbyModel`: slots, nome, plataforma, ping, pronto, expulsar, iniciar
- [ ] `[core][net]` Descoberta LAN por broadcast
- [ ] `[tools][net]` Servidor `rendezvous`: registro, código de sala, troca de endpoints
- [ ] `[core][net]` Hole punching pelo socket do ENet + fallback para relay
- [ ] `[core]` Início sincronizado: host novo jogo/carregar; clientes entram com scripts suprimidos
- [ ] `[core]` Desconexão/reconexão limpa (remoção de entidades do peer, migração de propriedade)

**Aceite:** hospedar e entrar por LAN, IP e código nas duas plataformas sem linha de comando; conexão
funciona com cliente atrás de CGNAT (via relay); visual indistinguível das páginas nativas.

## M3 — Combate e veículos

Objetivo: mundo livre completo a 4.

- [ ] `[core]` `EntityRegistry`: `NetId`, propriedade, relevância, `EntityCreate/Destroy/OwnershipChange`
- [ ] `[core]` `VehicleSync`: snapshot, correção, velocidades linear/angular, dano por evento
- [ ] `[win32][android]` Entrar/sair de veículo como motorista e passageiro; *drive-by*
- [ ] `[win32][android]` Troca de contexto para veículos com motorista remoto
- [ ] `[core]` População por cluster: gerador único, supressão de geração nos demais, migração
- [ ] `[core]` `PedSync` para peds ambientes (baixa taxa, por prioridade)
- [ ] `[core]` Tiros *hitscan*: evento do atirador, reprodução sem recalcular acerto
- [ ] `[core]` Dano: autoridade do dono da vítima, supressão de dano duplicado
- [ ] `[core]` Projéteis e explosões (autoridade do host para explosão)
- [ ] `[core]` Armas, munição, troca de arma, corpo a corpo
- [ ] `[core]` Morte/respawn em mundo livre; fogo amigo configurável
- [ ] `[core]` Interiores: id de interior no snapshot, relevância por interior
- [ ] `[core]` Orçamento de banda e priorização por distância × velocidade

**Aceite:** perseguição com 4 jogadores em 2 veículos e tiroteio por 30 min: sem dano duplicado, sem
veículos duplicados/fantasmas, ≤ 64 kbit/s por peer.

## M4 — Sincronização de missões

Objetivo: campanha completa.

- [ ] `[tools][script]` `data/opcodes.json` + `opgen`: assinaturas e classes de todos os opcodes usados
- [ ] `[script]` Decodificador de parâmetros do SCM (`DecodeParams`)
- [ ] `[script]` Hook de `ProcessOneCommand` no host; supressão de scripts nos clientes
- [ ] `[script]` Entidades de missão: create bloqueante, sempre relevantes, migração de *syncer*
- [ ] `[script]` `ScriptExecutor`: lista branca, validação de handles, fila pendente, thread proxy
- [ ] `[script]` Token de protagonista + hook de `CollectParameters`
- [ ] `[script]` Quantificadores `Any`/`All` para `PartyCondition`
- [ ] `[script]` Roteamento de `PlayerDirected` ao dono do ped
- [ ] `[script]` Barreiras (modelo, cutscene, áudio) com timeout
- [ ] `[script]` Cutscenes sincronizadas e pulo coordenado
- [ ] `[script]` `MissionState`: reagrupamento, falha (coop/clássico), espectador, reinício
- [ ] `[script]` `WorldBaseline`: serialização neutra do estado de mundo + opcodes `Persist`
- [ ] `[script]` `missions.json`: `shared`, `escort`, `solo_rotate`, coleira, papéis
- [ ] `[script]` Texto GXT ausente: fallback por string do host
- [ ] `[docs]` Matriz de compatibilidade missão a missão (ordem da campanha)
- [ ] `[script]` Telemetria (debug) de opcodes não classificados e handles inválidos

**Aceite:** campanha jogável do início aos créditos com 4 jogadores, com host PC e com host Android;
falha e reinício sincronizados em todas as missões da matriz; nenhum crash de cliente por entidade
ausente em 10 h de teste.
