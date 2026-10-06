# Snapcord — Definição do Projeto

> Este documento é a fonte de verdade do projeto. Todas as decisões abaixo já foram
> discutidas e fechadas com o dono do projeto — não reabra essas decisões sem motivo forte.

## Contexto para o Claude

- O dono do projeto **não programa e não quer mexer em código**. O Claude faz todo o
  trabalho técnico; o usuário decide, testa e aprova.
- Comunique-se **em português**, em linguagem simples, explicando o que foi feito e
  como testar.
- **Peça confirmação antes de** instalações grandes (SDKs, toolchains), ações no GitHub
  (criar repositório, push, releases) ou qualquer coisa difícil de desfazer.
- Trabalhe em passos pequenos e verificáveis: cada etapa deve terminar com algo que o
  usuário consiga rodar e ver funcionando.
- **Nunca** registre tokens, tickets de login ou dados de conta em logs, commits ou
  mensagens.

## Visão

Cliente de Discord **nativo, leve e de código aberto**, com **foco principal em chamadas
de voz** (mais importante que chat ou navegação de servidores). Layout fiel ao Discord
oficial, consumo mínimo de CPU e RAM, experiência completa no estilo do Ripcord.
Será distribuído publicamente.

## Decisões fechadas

| Item | Decisão |
|---|---|
| Nome | **Snapcord** (definitivo) |
| Licença | **GPLv3** |
| Hospedagem | GitHub, na conta do dono do projeto |
| Plataformas | **Windows, Linux e macOS desde o início**. Desenvolvimento principal no Windows, mas CI compila as três a cada mudança |
| Foco | **Voz** acima de tudo |
| Vídeo / compartilhamento de tela | **Fora do escopo** (não é prioridade) |
| Contas | Uma conta por vez |
| Login | **QR code** (protocolo Remote Auth, escaneado pelo app do celular) |
| Idioma do app | **Inglês por padrão**, com opção de **português (Brasil)** no menu de configurações |
| Idioma do código | README, comentários, nomes e scripts **em inglês** (projeto aberto internacional). A conversa com o dono do projeto continua em português |
| Visual | Layout **fiel ao Discord** (barra de servidores, lista de canais, painel de voz/usuário embaixo, área central, lista de membros). Tema escuro. Sem efeitos pesados ("eye candy") |

## Stack

| Área | Escolha | Observações |
|---|---|---|
| Linguagem | C++20 | A libdave (E2EE oficial do Discord) é C++ |
| Build / dependências | CMake + vcpkg (com CMakePresets) | Mesmo fluxo nas três plataformas |
| Interface | Qt 6 Widgets + QSS | Sem QML, sem WebView. Redesenha só o que muda |
| Rede | Qt Network + Qt WebSockets | Gateway, REST, voice gateway |
| UDP de voz | QUdpSocket ou socket nativo | Em thread própria |
| Captura/reprodução de áudio | **miniaudio** | WASAPI / CoreAudio / PipeWire-PulseAudio-ALSA |
| Codec | **Opus** (libopus) | 48 kHz, frames de 20 ms, PLC e FEC |
| Criptografia de transporte | `aead_aes256_gcm_rtpsize` / `aead_xchacha20_poly1305_rtpsize` | libsodium e/ou OpenSSL |
| E2EE | **libdave** (protocolo DAVE / MLS) | Obrigatório nas chamadas de voz do Discord |
| Supressão de ruído | RNNoise | Fase 3 |
| Cancelamento de eco | WebRTC AudioProcessing (opcional) | Fase 3 |
| Credenciais | QtKeychain | Credential Manager / Keychain / libsecret |
| Fonte | Fonte aberta parecida (Noto Sans ou Inter) | A fonte oficial (gg sans) é proprietária |

**Traduções:**
- Todo texto visível da interface passa por `tr()` e é escrito em inglês.
- A tradução fica em `translations/snapcord_pt_BR.ts` (formato Qt Linguist) e entra no app via `qt_add_translations`.
- Ao adicionar ou alterar um texto da interface, atualize também esse arquivo.
- O idioma escolhido fica salvo no QSettings (chave `language`) e vale ao reiniciar o app.

**Restrições:** não usar logo, marca nem a fonte proprietária do Discord. Qt é LGPL,
então o link tem que ser dinâmico.

## Metas de desempenho

- RAM: ~40–90 MB (sem e com chamada)
- CPU parado: ~0% (tudo orientado a eventos, sem polling)
- CPU em chamada: poucos % de um núcleo (Opus + RNNoise)
- Abrir em menos de 1 segundo
- Latência de voz equivalente à do cliente oficial

## Arquitetura

- **`core`** (sem interface): sessão, gateway, REST, autenticação, cache e modelos de dados.
- **`voice`**: motor de voz isolado em threads próprias. A thread de áudio em tempo
  real nunca bloqueia nem espera pela interface. Inclui:
  - voice gateway
  - UDP, descoberta de IP e RTP
  - criptografia e DAVE
  - Opus
  - jitter buffer
  - mixagem com volume por usuário
  - VAD e push-to-talk
- **`platform`**: tudo que depende do sistema operacional, atrás de interfaces:
  - hotkey global de push-to-talk
  - keychain
  - notificações
  - inicialização com o sistema
- **`app`**: interface Qt Widgets. Só desenha e repassa ações para o `core`/`voice`.
- **Cache enxuto:**
  - Guardar só o necessário em memória, com LRU de canais.
  - Membros carregados sob demanda.
  - Imagens decodificadas já no tamanho de exibição.
  - Animações pausadas quando fora da tela.
- **Gateway:**
  - Compressão `zlib-stream`.
  - Descartar cedo os eventos que nenhuma tela usa.
  - Identificação na conexão (IDENTIFY) e ritmo de requisições imitando fielmente o cliente oficial, para reduzir o risco de banimento.

### Estrutura de pastas proposta

```
snapcord/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── LICENSE                  (GPLv3)
├── README.md                (com aviso sobre os Termos de Serviço)
├── src/
│   ├── app/                 (main, janelas, widgets, QSS)
│   ├── core/                (auth, gateway, rest, cache, models)
│   ├── voice/               (voicegateway, udp, crypto, dave, opus, audio, mixer)
│   └── platform/            (win/, mac/, linux/)
├── resources/               (qss, ícones, fontes, sons)
├── tests/
└── .github/workflows/       (build Windows / Linux / macOS)
```

## Fases

### Fase 0: Ambiente e repositório
- Instalar Qt 6 (LTS), CMake, Ninja e vcpkg.
- Criar a estrutura, o LICENSE e o README.
- CI no GitHub Actions compilando para Windows, Linux e macOS.
- Uma janela vazia abrindo nas três plataformas.

### Fase 1: Núcleo de voz (prioridade máxima)
- Login por QR code, com o token guardado no keychain.
- Conexão ao gateway e lista de servidores e canais de voz.
- Entrar e sair de canal de voz, falar e ouvir, com **DAVE funcionando**.
- Mutar e ensurdecer.
- Indicador de quem está falando.
- Volume por usuário.
- Escolha de microfone e saída.
- Detecção de voz (VAD) com ajuste de sensibilidade, e push-to-talk.

### Fase 2: Qualidade de voz
- Supressão de ruído (RNNoise) e cancelamento de eco.
- Ajuste automático de ganho.
- Sons de entrar, sair, mutar e desmutar.
- Chamadas em DMs e grupos.
- Painel de conexão (ping, perda de pacotes).

### Fase 3: Chat
- Canais de texto e DMs.
- Markdown do Discord, menções, emojis e imagens.
- Mensagens não lidas e notificações.
- Respostas, reações, edição e exclusão.

### Fase 4: Lançamento nas três plataformas
- Instaladores:
  - Windows: `.exe`/MSI
  - macOS: `.dmg`
  - Linux: AppImage e Flatpak
- Atualização automática.
- Push-to-talk global em cada SO.
- Testes de áudio:
  - Linux: PipeWire e PulseAudio
  - macOS: permissão de microfone
- Releases no GitHub.

> Mesmo com o lançamento por último, o CI continua compilando Windows, Linux e macOS
> desde a Fase 0, para que nada específico de um SO entre escondido no código.

## Notas técnicas de referência

> Os protocolos do Discord não são documentados para clientes de usuário e mudam sem
> aviso. **Confira sempre em fontes atualizadas** antes de implementar: Discord
> Developer Docs (gateway e voz), o repositório discord/libdave, discord-userdoccers e
> clientes abertos como Abaddon e Discordo.

**Login por QR (Remote Auth):**
1. Conectar em `wss://remote-auth-gateway.discord.gg/?v=2` com o header `Origin: https://discord.com`
   (no Qt, o origin precisa ir no construtor do `QWebSocket`; header manual é ignorado e o Discord responde 403).
2. Gerar um par RSA-2048 e enviar a chave pública. O servidor responde com um nonce criptografado.
3. Descriptografar o nonce (RSA-OAEP/SHA-256) e enviar o nonce decifrado em base64url (não o hash).
4. Receber o `fingerprint` e mostrar o QR com `https://discord.com/ra/<fingerprint>`.
5. Após o scan, mostrar a prévia do usuário.
6. Após a confirmação no celular, receber o ticket.
7. Fazer `POST /users/@me/remote-auth/login` com o ticket e descriptografar o token recebido.

Pode aparecer captcha. É preciso tratar esse caso e ter um fallback.

**Voz:**
1. Pelo gateway principal (op 4, Voice State Update), receber `VOICE_STATE_UPDATE` e `VOICE_SERVER_UPDATE`.
2. Conectar ao voice gateway (v8) e fazer a descoberta de IP via UDP.
3. Enviar `Select Protocol` com o modo `*_rtpsize`.
4. Receber a chave de sessão.
5. Enviar RTP com Opus.
6. Aplicar a camada DAVE (opcodes próprios no voice gateway) por cima.

**Riscos conhecidos:**
- **Termos de Serviço:** clientes de terceiros violam os Termos do Discord e podem
  causar banimento. O README deve avisar isso claramente.
- **Manutenção:** o protocolo de voz e o DAVE podem mudar e exigir atualizações.
- **Push-to-talk global:**
  - Windows: tranquilo.
  - macOS: exige permissão de acessibilidade.
  - Linux com Wayland: depende do portal *GlobalShortcuts*; precisa de fallback.
- **Assinatura de código:**
  - Windows: sem certificado, o SmartScreen mostra aviso.
  - macOS: sem a conta de desenvolvedor da Apple (US$ 99/ano), o usuário precisa
    liberar o app manualmente.
  - É possível lançar sem assinar no início.

## Estado do ambiente (Windows)

| Ferramenta | Onde |
|---|---|
| Visual Studio 2022 Build Tools (MSVC) | Instalado no sistema |
| Qt 6.8.3 (MSVC 64-bit, com WebSockets e ImageFormats) | `C:\Users\Pedro\Qt\6.8.3\msvc2022_64` (via `aqtinstall`) |
| CMake e Ninja | Via pip, em `%APPDATA%\Python\Python314\Scripts` |
| vcpkg | `C:\Users\Pedro\vcpkg` (o baseline do `vcpkg.json` é o commit desse clone) |

- **Compilar:** `scripts\build.ps1` (Debug), `-Config release`, `-Run` para abrir.
  - O script entra no ambiente do MSVC e força o nosso vcpkg: o Developer Shell do VS troca
    `VCPKG_ROOT` pelo vcpkg embutido dele, que é antigo e quebra o build.
- **Executável:** `build\<config>\Snapcord.exe`.
- **Testes:** `build\<config>\snapcord_tests.exe` (Qt Test).
  - É um app de janela no Windows, então use `-o arquivo.txt,txt` para ver o relatório.
- **Log do app:** `%LOCALAPPDATA%\Snapcord\Snapcord\snapcord.log`.
  - A execução anterior fica em `snapcord.old.log`.
  - Nunca registrar tokens nem chaves.
- **Traduções:** depois de mudar textos, rode o alvo `Snapcord_lupdate` e confira se não
  sobrou `type="unfinished"` no `.ts`.

## Estado do projeto

- **Fase 0:** concluída.
- **Fase 1:** concluída e **testada com uma conta real**. O log da chamada confirmou:
  - DAVE v1 funcionando, com welcome aceito;
  - áudio indo e voltando;
  - zero falhas de decodificação.
  - As "E2EE encrypt failures" enquanto o usuário está sozinho no canal são normais: o grupo
    MLS só se forma quando chega o segundo participante.
- **Fase 2:** código completo, compila sem avisos e 15 testes passando. **Falta o teste real**
  das chamadas em DM, dos sons e do processamento de voz com o microfone.
  - **Processamento de voz (`AudioProcessor`):** pipeline em blocos de 10 ms, nesta ordem:
    1. Ganho de entrada.
    2. Cancelamento de eco (SpeexDSP MDF, cauda de 150 ms). A referência vem do áudio de
       saída, via `EchoReference`.
    3. Supressão de ruído RNNoise 0.2, compilado do tarball em `cmake/Dependencies.cmake`,
       com SSE4.1/AVX2 escolhidos em tempo de execução.
    4. Controle automático de ganho (preprocessador do SpeexDSP).
  - **Sensibilidade automática:** usa a probabilidade de voz do RNNoise (≥ 0,6) mais um piso
    de -60 dB.
  - **Sons (`SoundEffects`):** sintetizados em código, sem arquivos e sem os sons do Discord.
    Tocam num stream de saída próprio, aberto só enquanto há som tocando.
  - **Chamadas em DM e grupos:**
    - Capability `AUTO_CALL_CONNECT`; eventos `CALL_CREATE`, `CALL_UPDATE` e `CALL_DELETE`.
    - `VOICE_STATE_UPDATE` sem `guild_id`; o `server_id` da voz é o ID do canal.
    - Ao iniciar uma chamada nova, o cliente toca para os outros com
      `POST /channels/{id}/call/ring`; recusar usa `.../call/stop-ringing`.
  - **Painel de conexão:** clicar em "Voz conectada" mostra o gráfico de ping, a perda de
    pacotes recebidos (frames escondidos pelo PLC) e o estado do DAVE.

### Arquitetura implementada

- **`src/core`**
  - `RemoteAuth`: login por QR.
  - `Gateway`: zlib-stream, heartbeat, resume, backoff.
  - `Session`: READY, guilds, canais, permissões e voice states.
  - `RestClient`, `ClientProperties`, `Log`.
- **`src/voice`**
  - `VoiceConnection`: orquestra a chamada.
  - `VoiceGateway`: v8, com DAVE binário.
  - `DaveSession`: port do `DaveSessionManager.ts` da libdave.
  - `TransportCipher`: AES-GCM via OpenSSL e XChaCha via libsodium.
  - `UdpSocket`: sockets nativos e IP discovery.
  - `JitterBuffer`, `OpusCodec`, `AudioEngine` (miniaudio), `VoiceSettings`.
- **`src/platform`**
  - `CredentialStore`: Windows Credential Manager; stub nos outros sistemas.
  - `KeyState`: `GetAsyncKeyState` para o push-to-talk; stub nos outros sistemas.
- **`src/app`**
  - Telas: `LoginWindow`, `MainWindow`, `SettingsDialog`.
  - Componentes: `ServerRail`, `ChannelSidebar` (com delegate próprio), `VoiceChannelView`,
    `VoicePanel`, `UserPanel`.
  - Lógica: `VoiceController` (entrar, sair, mutar, ensurdecer), `ImageCache` (CDN com cache
    em disco), `AppController` (troca entre login e janela principal).
- **Threads da voz:**
  - O áudio do microfone é codificado e enviado direto no callback de captura.
  - Uma thread dedicada recebe o UDP e preenche jitter buffers por SSRC.
  - O callback de saída decodifica e mixa.
  - A sinalização roda na thread principal.
- **libdave:** entra via `FetchContent`, fixada no commit `8de72b1f`. A `mlspp` vem de um
  overlay port em `vcpkg/ports/mlspp`.

### Manutenção conhecida

- **Identificação do cliente:** `ClientProperties.cpp` imita o cliente desktop oficial
  (`client_version`, versões do Electron e do Chrome, `client_build_number`).
  - Esses valores foram definidos sem conferência e precisam ser atualizados de tempos em tempos.
  - O build number pode ser trocado sem recompilar pela chave `discord/clientBuildNumber`
    do QSettings.
- **Captcha no login por QR:** não é suportado e acontece na prática.
  - A alternativa é o **login por token**, na própria tela de login ("Log in with a token instead").
  - O token é validado com `GET /users/@me` antes de ser salvo.

### Fase 3 (chat)

- **Dados:**
  - `core/Message` é o modelo de mensagem.
  - `core/MessageStore` guarda no máximo 8 canais em memória (LRU) e carrega 50 mensagens por
    vez, mais o histórico sob demanda.
  - Mensagens enviadas aparecem na hora como "pendentes" e são trocadas pela versão do servidor
    quando o eco chega pelo gateway (casamento pelo `nonce`).
- **Markdown:** `core/Markdown` converte o markdown do Discord para o HTML que o QTextDocument
  entende. Fragmentos protegidos por marcadores Unicode de uso privado evitam formatar dentro de
  código, links e menções. Tem testes em `tests/MarkdownTest.cpp`.
- **Não lidas:** read states do READY, comparando o `last_message_id` do canal com o ID lido.
  - Ack com `POST /channels/{c}/messages/{m}/ack`, também pelo evento `MESSAGE_ACK`.
  - Menções contadas localmente.
  - `user_guild_settings` define o que está silenciado.
- **Interface:** `app/MessageView` traz o modelo, o delegate (layout cacheado por mensagem e
  largura) e a lista com hit-test. `app/ChatView` traz o cabeçalho, a lista e o compositor.
  Ainda há `EmojiPicker` e `Notifier` (bandeja do sistema mais o som `Message`).
- **Diagnóstico do microfone:** o log de voz traz quadros de voz enviados, quadros mutados, pico
  de nível e pico de probabilidade de voz, para investigar quando "ninguém me ouve".
- **Build com o app aberto:** `scripts\build.ps1 -Target <alvo>` compila só um alvo, útil quando o
  `Snapcord.exe` está aberto e não pode ser sobrescrito.

### Fase 4 (lançamento)

- **Fora de escopo por decisão do dono:** atualização automática, testes de áudio por plataforma e
  push-to-talk global no macOS/Linux.
- **Login guardado no sistema:** Windows Credential Manager, macOS Keychain e Linux Secret Service
  (libsecret), em `src/platform/CredentialStore*.cpp`.
- **Ícones:** gerados a partir do SVG pela ferramenta `snapcord_render_icons`
  (`tools/render_icons.cpp`, fora do build padrão). Os resultados ficam em `packaging/icons/` e vão
  para o repositório.
- **Metadados:**
  - `packaging/windows/snapcord.rc.in`: ícone e versão do `.exe`.
  - `packaging/macos/Info.plist.in`: inclui `NSMicrophoneUsageDescription`, obrigatório para o
    microfone funcionar no macOS.
  - `packaging/linux/*.desktop` e `*.metainfo.xml`.
  - ID do app: `io.github.pedrordgsr.Snapcord`.
- **Pacotes:**
  - Windows: `packaging/windows/package.ps1` gera o `.zip` portátil e o instalador Inno Setup
    (`snapcord.iss`).
  - macOS: `packaging/macos/package.sh` usa macdeployqt, assinatura ad-hoc e gera o `.dmg`.
  - Linux: `packaging/linux/package.sh` gera o AppImage com linuxdeploy e o plugin Qt.
  - Flatpak: manifesto em `packaging/flatpak/`, runtime KDE 6.8, dependências via vcpkg com rede
    liberada no build.
- **CI (`.github/workflows/build.yml`):**
  - Compila, testa e empacota nas três plataformas, mais o Flatpak.
  - Uma tag `v*` cria uma Release **rascunho** com todos os pacotes.
  - Quando falha, o passo "Report errors" publica as linhas de erro como anotações, que são
    públicas. Assim dá para ler pela API sem login:
    `GET /repos/pedrordgsr/snapcord/check-runs/{job_id}/annotations`.
- **Verificado localmente:**
  - O `.zip` do Windows tem 15 MB, roda sem o Qt instalado e ocupa ~34 MB de RAM em Release.
  - Linux, macOS e Flatpak só podem ser verificados pelo CI.

## Próximo passo

1. O dono do projeto testa a Fase 2:
   - chamada em DM, tanto iniciando quanto recebendo;
   - supressão de ruído e cancelamento de eco;
   - sons;
   - painel de conexão.
2. Se algo falhar, ler o `snapcord.log` para diagnosticar.
3. Depois, seguir para a Fase 3 (chat).
