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
1. Conectar em `wss://remote-auth-gateway.discord.gg/?v=2` com o header `Origin: https://discord.com`.
2. Gerar um par RSA-2048 e enviar a chave pública. O servidor responde com um nonce criptografado.
3. Descriptografar o nonce (RSA-OAEP/SHA-256) e enviar a prova.
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

## Estado do ambiente (Windows, levantado em 2026-10-06)

| Ferramenta | Situação |
|---|---|
| Visual Studio 2022 Build Tools (MSVC) | Instalado |
| Git | Instalado |
| Python 3.14 + pip | Instalado |
| Rust/cargo | Instalado (não será usado) |
| Qt 6 | **Não instalado** |
| CMake | **Não instalado** |
| Ninja | **Não instalado** |
| vcpkg | **Não instalado** |
| Espaço livre em C: | ~33 GB |

Sugestão para a Fase 0: instalar o Qt via `aqtinstall` (pip), e o CMake e o Ninja via
pip ou winget, tudo na pasta do usuário. **Confirmar com o usuário antes de instalar.**

## Próximo passo

Começar pela **Fase 0**. Antes de qualquer instalação, apresente o plano ao usuário e
peça confirmação.
