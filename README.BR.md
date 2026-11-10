[English](README.EN.md) · **Português (BR)**

# Outpost 2: Divided Destiny — Port Moderno (C++17 + SDL2)

Reimplementação de *Outpost 2: Divided Destiny* (Sierra On-Line / Dynamix, 1997)
em **C++17 + SDL2**, reconstruída por engenharia reversa a partir do
disassembly do executável original.

**O jogo roda de ponta a ponta**: as campanhas das duas facções, os jogos de
colônia, os 14 tutoriais e partidas multijogador em rede para até 6 jogadores.
As 94 DLLs de missão originais são carregadas e **executadas de verdade**, por
um carregador PE próprio com pontes de ABI para os 222 símbolos que elas
importam do `OUTPOST2.EXE`.

> **Diretiva do projeto:** todo mecanismo é extraído do **código original**
> (disassembly e binários) — nada é adivinhado. Onde o original tem um defeito
> identificado, o porte **corrige em vez de reproduzir**.

Veja [`PORT_STATUS.BR.md`](PORT_STATUS.BR.md) para o estado detalhado: o que está
pronto, o que falta, e o mecanismo original por trás de cada parte.

---

## ⚠️ Nada do jogo de 1997 está aqui

Este repositório contém **apenas o código do porte**. Os arquivos do jogo
(`OUTPOST2.EXE`, as `*.DLL`, os `*.VOL`, `OP2_ART.BMP`, os `*.ANI`,
`OUTPOST2.INI`, `OUTPOST2.HLP`…) e o disassembly são material comercial
protegido e **não** são versionados nem redistribuídos.

O porte **lê** esses arquivos de onde eles já estão e não modifica nenhum. Para
jogar é preciso ter uma cópia legítima do jogo instalada.

---

## Requisitos

| | | |
|---|---|---|
| Sistema | **Windows** 10 ou 11 | o porte usa a API do Windows (carregador PE, Winsock, GDI) |
| Arquitetura | **Win32 / x86 — obrigatório** | nem preferência nem padrão: veja o porquê abaixo |
| Compilador | MSVC com C++17 (Visual Studio **2019 ou mais novo**) | o código usa `__fastcall` e o ABI da Microsoft; MinGW/Clang não foram testados |
| CMake | **3.15 ou mais novo** | testado também com o CMake 4.x |
| SDL2 | pacote de desenvolvimento **2.30.9 para VC** | a única dependência externa do executável |
| Jogo original | **obrigatório**, instalado | os assets de 1997 são lidos do disco |
| Python 3 | opcional | só para os scripts de `tools/` |

Não há vcpkg, Conan nem submódulos. `ws2_32` e `ole32` (rede) e `gdi32`
(fontes) são resolvidos pelo próprio MSVC — nada a instalar por eles.

### Por que o build x86 é requisito

O porte carrega as DLLs de missão originais e **executa o código de 32 bits
delas dentro do próprio processo** (`src/mission/pe_loader.cpp`), com pontes de
API na convenção `__fastcall` do x86 (`src/mission/op2_api.cpp`). Em um
processo de 64 bits isso é impossível: um build x64 **compila** e só falha ao
abrir a primeira missão. Para a armadilha não chegar até lá, o `CMakeLists.txt`
para na configuração se a arquitetura não for 32 bits.

---

## Instalando as dependências

### 1. Compilador (MSVC)

Instale o **Visual Studio Community** (gratuito, 2022 ou mais novo) —
<https://visualstudio.microsoft.com/downloads/> — e, no instalador, marque o
workload **"Desenvolvimento para desktop com C++"** (*Desktop development with
C++*). Ele já traz tudo o que o porte precisa: o compilador MSVC, o Windows
SDK e o **suporte a x86**, que é o alvo usado aqui.

Quem não quer a IDE pode instalar só as ferramentas de linha de comando
(*Build Tools for Visual Studio*, mesma página). Pelo winget:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

### 2. CMake

O workload de C++ do Visual Studio inclui um CMake. Para ter o comando `cmake`
disponível em qualquer terminal, instale a versão avulsa:

```powershell
winget install --id Kitware.CMake
```

Ou baixe de <https://cmake.org/download/> marcando *"Add CMake to the system
PATH"*. Confira com `cmake --version` (precisa ser 3.15+).

### 3. SDL2

Baixe **`SDL2-devel-2.30.9-VC.zip`** — o pacote *de desenvolvimento* para
Visual C++, não o de runtime — em
<https://github.com/libsdl-org/SDL/releases/tag/release-2.30.9>.

Extraia dentro do repositório de modo que a árvore fique **exatamente** assim:

```
third_party/
  SDL2-2.30.9/
    include/          SDL.h, SDL_render.h, ...
    lib/
      x86/            SDL2.lib, SDL2main.lib, SDL2.dll   <- as que o build usa
      x64/            (vem no pacote; não é usada)
```

Esse caminho é o `SDL2_ROOT` do `CMakeLists.txt`. Se preferir o SDL2 em outro
lugar, mude essa variável lá. A pasta `third_party/` não é versionada — é por
isso que este passo é manual.

No PowerShell, do diretório do repositório:

```powershell
mkdir third_party
Expand-Archive -Path "$HOME/Downloads/SDL2-devel-2.30.9-VC.zip" -DestinationPath third_party
```

### 4. Python, só se for usar os `tools/`

Os 20 scripts de engenharia reversa em `tools/` pedem **Python 3**. A maioria
usa só a biblioteca padrão; nove deles precisam de dois pacotes:

```powershell
pip install pefile capstone
```

`pefile` (ler recursos e exports de PE) é usado por `analyze_shell.py`,
`check_dialog_fmt.py`, `disasm_menu.py`, `dump_dialogs.py`, `extract_bmps.py`,
`extract_menu.py`, `inspect_bmps.py`, `inspect_table.py` e `parse_dlg.py`;
`capstone` (desassemblador) pelos dois primeiros. **Nada disso é necessário
para compilar ou jogar.**

---

## Build

Com as dependências no lugar, do diretório do repositório:

```powershell
cmake -B build -S . -A Win32
cmake --build build --config Release
```

O `-A Win32` não é opcional. Se for omitido em uma máquina de 64 bits, a
configuração para com uma mensagem dizendo o que fazer.

O passo de pós-build copia `outpost2_modern.exe` e `SDL2.dll` para a raiz do
projeto. **O executável tem de rodar no diretório de instalação do jogo** — é
de lá que ele lê os `.VOL`, as DLLs de missão e os recursos originais. O jeito
mais simples é clonar o repositório **dentro da pasta do jogo**: o `.gitignore`
é uma lista branca, feita exatamente para isso, e ignora todo o material
original que convive com os fontes.

### Se der errado

| Sintoma | Causa |
|---|---|
| `Outpost2Port precisa ser gerado para Win32 (x86)` | faltou o `-A Win32` |
| `SDL.h` não encontrado, ou `SDL2.lib` inexistente | o SDL2 não está em `third_party/SDL2-2.30.9/` (confira o nome da pasta) |
| `MSB3073` / *"Permission denied"* ao copiar o exe | o jogo está aberto; feche e refaça o build |
| compila, mas toda missão falha ao abrir | o build saiu x64 — apague `build/` e configure de novo com `-A Win32` |

> Filtrar a saída do build só por `"error C"` **esconde** a falha de cópia do
> pós-build. Filtre por `error|denied`.

---

## Rodando

```powershell
outpost2_modern.exe                      # menu principal
outpost2_modern.exe --game eden01.map    # abre um mapa direto
```

O arquivo `OUTPOST2.INI` do jogo é respeitado. O porte acrescenta oito chaves
que o original não tem — nível da IA, escuridão da noite, volume, câmera do
demo, tela cheia, a pele dos menus (`MenuSkin`), a volta horizontal do mapa
(`MapWrap`) e a última linha do mapa (`ShowBottomRow`); estão documentadas no `PORT_STATUS.BR.md`.

Há **105 modos de linha de comando** para testes e capturas, entre eles uma
bateria de regressão (`--savetest` prova o determinismo de salvar/recarregar).
A lista está no `PORT_STATUS.BR.md`.

---

### Cheats

Os cheats do original não são teclas: são frases escritas na **linha de
conversa** (Enter). Acertando, o texto vira `Woof Woof!` e o efeito vale para
quem digitou. **Só funcionam em jogo solo**: em rede ou em cenário
multijogador a frase não faz nada e segue como mensagem comum — o original
deixava valer em qualquer partida, o que estraga o jogo dos outros.
**A pontuação faz parte da senha:**

| Frase | Efeito |
|---|---|
| `Dan's dog could make more ore.` | enche os dois estoques até a capacidade |
| `Dan's car could hold more people.` | trabalhadores e cientistas +25% |
| `Dan's bike could backfire more often` | armas recarregam mais rápido |
| `Dan's gun could die more people.` | armas com muito mais penetração |

As três primeiras são do jogo de 1997. A quarta é um **extra do porte**: o
efeito existe no original, mas a frase dele nunca foi publicada e o
executável só guarda o hash — então esta frase foi criada para alcançá-lo. O
`PORT_STATUS.BR.md` traz o reconhecedor inteiro, os hashes e o que foi conferido
e descartado das listas de cheat que circulam por aí.

---

## Estrutura

```
src/
  main.cpp        entrada + os 105 modos de teste/captura
  app/            TApp, UIState, GameState (o jogo), ShellState, HUD,
                  as 6 abas do quadro, minimapa, globo, diálogos
  game/           a simulação: unidades, estruturas, movimento, pathfinder,
                  combate, economia, pesquisa, desastres, triggers, IA
  gfx/            framebuffer do tamanho da janela (base 640x480), PBMP, sprites, fontes (bitmap e GDI)
  res/            leitor de PE (.rsrc + exports), .VOL + LZHUF, ResManager
  core/           OUTPOST2.INI (CConfig)
  audio/          mixer SDL fiel ao DirectSound do original, música (.clm)
  mission/        carregador PE das DLLs de missão + ponte da API
  net/            lobby (descoberta/entrada) e partida em lockstep
tools/            scripts Python de engenharia reversa (formatos, dumps)
```

São 45.200 linhas de C++17. Os formatos de 1997 estão todos decodificados e
documentados no `PORT_STATUS.BR.md`: o container `.VOL` (com o LZHUF), o `.map`,
os tilesets PBMP, os sprites do `op2_art`, as planilhas de dados e os
cursores `.ANI`.

---

## Mudanças recentes

### 2026-11-10

- **A última linha do mapa fica escondida**, como no original: a vista para
  no fundo do clip rect do mapa (`altura - 2` nos mapas do jogo), então a
  última linha de tiles nunca aparece. `ShowBottomRow=1` no `OUTPOST2.INI`
  deixa a vista descer até ela.

### 2026-11-08

- **A vista do mapa para nas bordas**, como no original: só os mapas "around
  the world" (512 tiles de largura) dão a volta. Unidades, terreno e a câmera
  do modo demonstração seguem a mesma regra. `MapWrap=1` devolve a volta em
  todo mapa, que era o comportamento antigo.
- **Par de rádio `Screen Mode`** — tela cheia ou janela — nas **duas** telas de
  preferências: a `PREFERENCES` do menu principal e a lista do `PREFERENCES` da
  aba Options, dentro do jogo. Só vale no `ACCEPT`/`&APPLY`, e grava
  `[Game] Fullscreen` no INI.
- **A lista do `PREFERENCES` de dentro do jogo escondia as próprias setas de
  rolagem** atrás dos botões: nada depois da primeira página era alcançável, e
  o painel ainda roubava o clique do `L&ESS`.
- **As dicas dos botões `L&ESS`/`&MORE`** passaram a ser as do original, uma
  por item ("Turn music volume down (L&ess)"), e não uma frase genérica.
- **A Meteor Defense não derruba mais o míssil de um aliado.** Só poupava o
  míssil do próprio dono, então numa partida aliada a defesa do amigo abatia o
  míssil que ia no inimigo.
- **O muro não falha mais** por ter perto dele um muro em obra, a praga ou
  lava. Prédio vizinho continua impedindo, como deve.
- **O `Ctrl+P` pausa o jogo** (e a tecla `PAUSE` também, como no acelerador do
  original): o relógio para, a música e o som calam, e aparece o letreiro
  *"Game Paused. Press CTRL+P to Resume"*. Em partida multijogador não pausa —
  sai o aviso *"Cannot Pause in multiplayer games"* na barra de estado.
- **"MISSION ACCOMPLISHED" e "MISSION FAILED" voltaram a ser o letreiro do
  jogo:** Arial negrito grande, **vermelho**, com o contorno preto de 2 px —
  o porte os escrevia com a fontinha de depuração, amarelos e pequenos.
- **O ESC pergunta antes de largar a missão.** Antes ele voltava ao menu na
  hora; agora desiste primeiro do que estiver no ar (uma mira de comando, um
  diálogo) e, se não houver nada, abre a tela de fim de missão do próprio jogo
  com a pergunta *"Are you sure you want to abort the mission?"*.
- **O demo de atração não entra mais de surpresa.** O relógio do minuto de
  ociosidade recomeça do zero toda vez que o menu volta — depois de um tempo
  jogando, sair da partida caía direto no demo.
- **Os cursores do jogo entraram.** Mover, atacar, atracar, reparar,
  reprogramar, terraplanar, guardar, patrulhar, recolher e autodestruir têm o
  cursor animado do Outpost 2, cada um com a sua versão de "não pode" — o porte
  usava a seta do sistema em tudo.
- **Clicar em qualquer parte de um prédio o marca.** Antes só o miolo
  respondia — o clique usava um raio de 20 px do centro, igual para tudo, e num
  prédio de 3×2 tiles isso deixava a maior parte dele morta.
- **A vista rola pela borda da tela**, como em qualquer RTS: encostar o
  ponteiro na beirada da janela move o mapa, com aceleração, e o minimapa
  acompanha. As setas fazem o mesmo, pelo mesmo caminho — e a opção
  *Scrolling Speed* do `PREFERENCES`, que o porte lia e não usava, finalmente
  manda na velocidade.
- **Os cheats passaram a valer só em jogo solo.** No original a frase funciona
  em qualquer partida, inclusive em rede — e lá um jogador enchendo o próprio
  estoque estraga a partida dos outros.
- **O relógio da simulação subia cedo demais.** O jogo levanta o contador de
  tiques só no fim do ciclo, depois do laço de unidades; o porte levantava
  antes, e tudo que depende de `(id ^ tique)` ficava um tique fora de fase.
- **O HUD de depuração sorteava um número por tique**, deslocando a sequência
  do gerador — todo sorteio seguinte (dano de arma, por exemplo) saía diferente
  do original. Agora ele lê o estado do gerador em vez de sortear.
- **Plataformas de doca, respiradouros de magma e o zoom do minimapa**: as
  plataformas das minas, tanques e Agridome passaram a sair do passe de camada
  de chão do original (com dano), os respiradouros de magma passaram a ser
  desenhados, e o minimapa abre encaixando o mapa inteiro, como no jogo.
- **Caminhão carrega carga parcial.** O jogo só deixava carregar com estoque
  para encher o caminhão, então os últimos 900 de metal ficavam presos no
  depósito. Junto disso, objetivos do tipo "ter 5 caminhões de metal" passaram
  a valer pela **quantidade**: cinco caminhões somando 4900 não fecham.
- **A praga nunca infectava prédio por chamada de missão**: o `DoInfect`
  saía sem fazer nada quando a partida tinha mais de um jogador — ou seja, em
  quase toda missão de campanha.
- **Destroço de nave posto sobre entulho era recolhido como entulho**, e o
  tile ainda era terraplanado, levando o destroço junto.
- **`ForceMorale*` das missões só pegava na segunda chamada**, e a primeira
  deixava a colônia em `Great` em vez do nível pedido.
- **"Perma-walls" invisíveis**: o Earthworker que morria levantando um muro
  deixava a célula reservada para sempre, bloqueando o caminho sem aparecer.
- **O muro deixou de ser assentado por cima de quem está no tile**: agora
  cancela e devolve o metal, como já fazia com o tubo.
- **Avisos do Savant que não param de repetir.** "Power Levels: Optimal" e
  "Storage Needed" saíam a cada recálculo da colônia; agora só na mudança de
  estado. Os avisos de falta continuam insistindo, como devem.
- **Transferência de/para jogador de PC não avisa mais.** Script de missão
  transfere unidade o tempo todo, e cada uma virava um aviso na tela.
- **Avisos sumidos nos dois primeiros minutos de partida** (defeito do porte,
  não do original): o filtro anti-repetição comparava com um relógio que começa
  em zero, e barrava o primeiro "Storage Needed" e o "Structure Disabled" de
  cada partida.
- **Teto de 2048 unidades vivas.** Não havia teto, e o id da unidade precisa
  caber nos 11 bits da ocupação da célula do mapa (máximo 2047): passar disso
  não daria "cheio", daria ocupação truncada.

### 2026-11-07

- **Mover com o botão direito errava o tile e nunca atracava.** O alvo saía das
  coordenadas da janela em vez das da vista do mapa, então as unidades iam para
  um ponto 12 px à direita e 22 px abaixo do clique — quase sempre o tile de
  baixo. E o caminho pulava a lógica de mover do original (`sub_451280`), então
  clicar na doca de um prédio nunca mandava a ordem de atracar. Agora usa as
  coordenadas da vista e o mesmo caminho do botão MOVE.
- **Doca do Agridome.** O Agridome tem doca de caminhão (`BUILDING.TXT`:
  Docking Truck = Y), mas a arte dele não traz a plataforma, e os caminhões
  paravam em chão vazio. Ele ganhou a mesma plataforma que os outros prédios
  com doca carregam na arte (imagens 385–388, posicionadas como nos 10 grupos
  de Structure Factory / Smelter / Spaceport / GORF), nas duas facções e só
  depois que o prédio fica pronto.
- **A tela do jogo acompanha o tamanho da janela.** Antes era uma imagem fixa
  de 640×480 esticada, então uma janela maior só deixava tudo maior. Agora o
  jogo é desenhado no tamanho real da janela: a coluna da direita (minimapa,
  abas, painel) mantém os 192 px presa à borda direita, a vista do mapa ocupa o
  resto, e redimensionar a janela ou alternar a tela cheia refaz o layout. Os
  menus continuam em 640×480 escalados, como no original. A moldura vai até a
  borda real, e o diálogo de salvar/carregar e a caixa "Success" ficam
  centralizados.
- **Chave `MenuSkin`**, para escolher o estilo dos menus. `0` (padrão) é o
  visual do próprio porte — painéis azul-escuros, menu principal translúcido à
  direita. `1` é a **pele de diálogo do Outpost 2 original**, com toda a arte
  lida do `OP2SHRES.DLL` do jogo: fundo azul-marinho "blueprint" (bitmap 225),
  borda vermelha e barra de título com Arial negrito verde; botões com a arte
  vermelha do jogo (bitmaps 235/236) e o contorno vivo no foco; listas pretas
  com seleção azul; menu principal medido do original (192×348). Sem os
  bitmaps, todo controle volta ao estilo do porte.
- Novos modos de teste: `--dockclicktest <DLL>`, que clica com o botão direito
  na doca de um Agridome com um caminhão pelo caminho real de entrada e confere
  que ele atraca, e depois que um clique em tile livre chega exatamente lá; e
  posição opcional de câmera no `--missiontest`. As capturas passaram a sair no
  tamanho real do quadro.

### 2026-11-06

- **Os itens do `END MISSION` não faziam nada** — dois defeitos somados e três
  divergências com o original. Ver a seção 5.7 do `PORT_STATUS.BR.md`.
- **Cheats digitados na linha de conversa**, como no original, mais um extra do
  porte para o quarto efeito. Ver a seção de cheats acima.
- **Documentação em inglês**: `README.EN.md` e `PORT_STATUS.EN.md`. Os
  originais em português viraram `README.BR.md` e `PORT_STATUS.BR.md`, e o
  `README.md` é uma página de entrada para os dois idiomas.

---

## Licença

O código deste porte está sob a licença **MIT** (veja [`LICENSE`](LICENSE)).

A licença cobre **só o código do porte**. Ela não alcança nada de *Outpost 2:
Divided Destiny* — nem o executável, nem as DLLs, nem os `.VOL`, a arte, os
sons, os mapas ou o disassembly usado como referência. Nada disso está neste
repositório, e uma cópia legítima do jogo continua sendo requisito para rodar.

## Créditos

*Outpost 2: Divided Destiny* é de Sierra On-Line / Dynamix. Este é um trabalho
independente de preservação e reimplementação, sem relação com os detentores
dos direitos.

Os projetos **OPUPatch** e **TethysAPI**, da comunidade, foram usados como
referência de documentação — para conferir nomes de classes, offsets e a lista
de defeitos conhecidos do original. **Nenhum código deles foi copiado para
cá**, por serem de licença distinta.

Reverse Engineering done by Claude Code!
