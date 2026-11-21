# Changelog

Mudanças do porte, agrupadas por data — a mais recente primeiro. O detalhe
técnico de cada uma está no [PORT_STATUS.BR.md](PORT_STATUS.BR.md).

*Changes to the port, grouped by date — most recent first. The technical
details of each one are in [PORT_STATUS.BR.md](PORT_STATUS.BR.md)
(Portuguese).*

---

## 2026-11-21

### Português

**Novo**

- **Atalhos O (carregar) e U (descarregar) na doca** (opção **Dock Hotkeys
  (O/U)**, nas preferências e na aba Options; ou `DockHotkeys=1` na seção
  `[Game]` do `OUTPOST2.INI`). Com uma estrutura selecionada e um veículo na
  doca dela, o **O** carrega e o **U** descarrega sem abrir a tela de itens.
  São as letras do L&OAD / &UNLOAD do original.
  - **Structure Factory:** o O passa ao ConVec o kit da primeira baia
    ocupada; o U guarda o kit dele na primeira baia vazia.
  - **Garagem:** o U guarda o veículo da doca, o O tira o primeiro guardado.
  - **Agridome, smelters e tanques:** carregam e descarregam o caminhão.

  Desligada (o padrão), é o jogo original.

**Corrigido**

- O nome da opção da roda do mouse aparecia como "Wheel  Scroll Bars" (o "&"
  sumia). Agora é **Wheel/Scroll Bars**.

**Para quem acompanha o código:** seção 5.55 do PORT_STATUS; o modo de teste
`--dockhotkeytest`.

### English

**New**

- **O (load) and U (unload) dock hotkeys** (the **Dock Hotkeys (O/U)**
  option, in the preferences and the Options tab; or `DockHotkeys=1` in the
  `[Game]` section of `OUTPOST2.INI`). With a structure selected and a
  vehicle on its dock, **O** loads and **U** unloads without opening the
  items screen. These are the letters of the original's L&OAD / &UNLOAD.
  - **Structure Factory:** O hands the ConVec the kit from the first
    occupied bay; U stores its kit in the first empty bay.
  - **Garage:** U stores the docked vehicle, O takes out the first stored
    one.
  - **Agridome, smelters and tanks:** they load and unload the truck.

  Off (the default), it is the original game.

**Fixed**

- The mouse wheel option's name showed as "Wheel  Scroll Bars" (the "&" was
  lost). It is now **Wheel/Scroll Bars**.

**For those following the code:** section 5.55 of PORT_STATUS; the
`--dockhotkeytest` test mode.

---

## 2026-11-20

### Português

**Corrigido**

- **A lista de objetivos que sumia no meio da missão.** Na missão 05 do Eden
  os objetivos mudam o tempo todo: os novos entram no topo e os cumpridos
  saem. Com a lista rolada para baixo, ela encolhia e a janela ficava com uma
  linha só ou vazia, e os objetivos restantes sumiam da vista. Agora, como no
  original, a lista acompanha o objetivo que estava no topo, e a janela nunca
  fica vazia enquanto houver objetivos para mostrar. A tela "Communications
  Satellite: Not Operational" é a do Space Program, o quinto botão das abas;
  os objetivos são o sexto, o mais à direita.
- **A tela de briefing como no original.** No original, o briefing de cada
  missão da campanha mostra o texto numa caixa estreita à esquerda e uma
  imagem da missão à direita (projetos, mapas, a projeção da praga…). O porte
  mostrava só o texto, numa caixa da largura da tela. Agora a imagem aparece
  no lugar certo, cada missão com a sua (12 do Eden e 12 do Plymouth). As telas
  de objetivos e de história continuam com a caixa larga, como no original.
- **O texto do briefing rola pelo mouse.** A barra de rolagem da caixa de
  texto era só desenho: agora as setas rolam uma linha, o clique na trilha
  rola uma página, e o polegar se arrasta (setas e trilha repetem com o botão
  seguro). A roda do mouse passou a funcionar também nos mouses de roda fina e
  nos touchpads, que antes não rolavam nada. Vale no briefing, nos objetivos e
  na história.
- **Barra de rolagem e roda em todas as listas com texto dos menus.** Os
  tutoriais, o manual (a lista de tópicos e o texto, que agora tem barra), o
  placar e os capítulos passaram a rolar pela roda (a lista sob o cursor) e
  pela barra (setas, trilha e arrasto), como as listas do Windows do original.
  Na lista de capítulos, os últimos ficavam cortados a partir da missão 12, sem
  como rolar.
- **A rolagem do mapa para cima e para baixo pela borda.** Em janela
  maximizada, levar o cursor à borda de cima ou de baixo quase não rolava o
  mapa (para os lados era normal): o cursor passava para a barra de título ou
  para a barra de tarefas, saía da janela e a rolagem parava. Como no original,
  o cursor além da borda agora continua rolando.
- **Missão 05 do Eden: "Have 3 or more empty Cargo Trucks".** Esse objetivo
  nunca se cumpria, nem com os caminhões vazios na colônia: a contagem de
  caminhões com um tipo de carga passou a ser pela quantidade carregada (para
  a carga parcial), e um caminhão vazio tem quantidade zero. Agora "vazio"
  volta a contar caminhões. Os 22 objetivos da missão foram conferidos um a um
  — inclusive o lançamento do satélite — até o MISSION ACCOMPLISHED.

**Conferido**

- **O que cada pesquisa libera.** Uma auditoria percorreu a árvore de
  pesquisas inteira, do Eden e do Plymouth. Cada pesquisa libera os seus
  prédios e veículos só depois de concluída, e aplica todas as melhorias que
  promete. A cadeia da reciclagem foi testada de ponta a ponta:
  - Metals Reclamation libera o kit do GORF;
  - com o GORF funcionando, o caminhão vazio ganha o comando de recolher
    destroços (SALVAGE);
  - Recycler Postprocessing aumenta o metal recuperado de 50% para 60%.

  Nenhum problema encontrado.

**Novo**

- **Barra de rolagem nas listas da partida** (opção **List Scroll Bars**, nas
  preferências e na aba Options; ou `ListScrollBars=1` na seção `[Game]` do
  `OUTPOST2.INI`). No original as listas da partida só têm as setas de página.
  Com a opção ligada, cada uma ganha uma barra arrastável no lugar das setas,
  e a roda do mouse rola a lista sob o cursor: relatórios (fábricas,
  laboratórios, recursos, comunicações, espaçoporto), opções, objetivos,
  pesquisa, produção e lista de veículos. Com ela, a roda do mouse também
  **rola o mapa** (2 tiles por degrau; o arrasto lateral do touchpad, para os
  lados). Na tela de preferências ela se chama **Wheel/Scroll Bars**.
  Desligada (o padrão), é o jogo original.

- **O modo `--harness` roda os roteiros do OP2TestHarness quase por
  inteiro.** O [OP2TestHarness](https://github.com/leviathan400/OP2TestHarness)
  joga um cenário no jogo original e grava o que aconteceu; o `--harness` joga
  o mesmo roteiro no porte, para comparar os dois. Antes o porte só conhecia
  15 verbos e 89 dos 478 cenários do op2-rs paravam em verbos que ele não
  tinha. Agora ele tem os de economia e de colônia (`beacon`, `tube`, `wall`,
  `player`, `tech`, `mem`, `poke`, `route`, `produce`, `build`, `kit`, `bay`,
  `cargo`, `transfercargo`, `load`/`unload`, `idle`/`unidle`, `train`,
  `research`, `launch`), as obras (`doze`, `buildwall`, `removewall`), os
  comandos com alvo (`repairobj`, `reprogram`, `dismantle`, `scatter`), os
  desastres (`meteor`, `quake`, `eruption`, `lightning`, `tornado`, `blight`,
  `empmissile`, `lavaspeed`, `lavapossible`, com o `now` que pula os avisos)
  e os grupos de IA das missões (`group`). Cada um manda o mesmo pacote ou faz
  a mesma chamada da API de missão que o harness faz no original. Fora ficam
  só os de mouse e teclado (`click`, `drag`), que viram uma entrada real na
  janela do jogo. Verbos que o próprio harness não conhece (`salvage`,
  `groupmove`) dão o mesmo "unknown verb" de lá.

- **O `--harness` grava a telemetria (`telemetry.jsonl`)**, como o
  OP2TestHarness grava no original: com `--telemetry-every N`, uma linha no
  tick 1, a cada N ticks e no fim, com a colônia de cada jogador (comida,
  minério, população, moral, energia, prédios, tecnologias) e todos os prédios
  e veículos (tipo, dono, tile, HP, comando). Com `--eco`, também a alocação
  da colônia (trabalhadores e cientistas livres e exigidos, energia, comida,
  capacidades, as entradas da moral), o estado de cada unidade (flags, ação e
  seu tempo, carga do caminhão, doca) e os beacons de mineração. São os mesmos
  nomes de campo e as mesmas coordenadas do original, para comparar os dois
  campo a campo (167 cenários do op2-rs gravam a telemetria `--eco`). Os
  campos que o porte não modela ficam de fora em vez de saírem como 0. E o
  roteiro agora roda em ordem de tick, como no harness: uma linha do tick 1
  listada depois de linhas de ticks maiores esperava por elas (em
  `chaos_big_colony` a população só chegava no tick 17085).

- **O trace do `--harness` (`trace.jsonl`) tem todos os campos do
  OP2TestHarness.** Além da posição, direção, HP e arma, cada unidade traz
  agora o comando e a ação (pelos nomes do original), a rota em curso (o
  destino, o passo, a fração do pixel, a velocidade, os pontos e as direções
  que faltam), os efeitos de EMP, espuma e ESG e as casas que ela ocupa. Cada
  linha traz também os disparos das unidades do cenário, todos os outros
  objetos vivos (explosões, animações, unidades que o roteiro não criou), o
  alocador de objetos (o maior índice e o anel de livres) e as unidades mortas
  cujo índice já é de outra. A primeira linha sai no tick 1, como no harness,
  e o verbo `trace` grava uma na hora. Os nove cenários de referência seguem
  iguais; nos duelos, todos os campos novos batem com o original em todo
  tick.

**Para quem acompanha o código:** seções 5.48 a 5.52 do PORT_STATUS; os modos
de teste `--brieftest`, `--listscrolltest`, `--panescrolltest`,
`--missionscrolltest`, `--e05test`, `--objdump`, `--techunlocktest` e
`--e05objtest`; seções até a 5.54.

### English

**Fixed**

- **The objectives list that vanished mid-mission.** In Eden mission 05
  objectives change all the time: new ones enter at the top and completed
  ones leave. With the list scrolled down, it shrank and the window showed a
  single line or nothing, and the remaining objectives went out of view. Now,
  as in the original, the list follows the objective that was at the top, and
  the window is never empty while there are objectives to show. The
  "Communications Satellite: Not Operational" screen is the Space Program,
  the fifth tab button; objectives are the sixth, the rightmost.
- **The briefing screen as in the original.** In the original, each campaign
  mission's briefing shows the text in a narrow box on the left and a picture
  of the mission on the right (designs, maps, the blight projection…). The
  port showed only the text, in a box as wide as the screen. The picture now
  appears in the right place, each mission with its own (12 Eden, 12
  Plymouth). The objectives and story screens keep the wide box, as in the
  original.
- **The briefing text scrolls with the mouse.** The text box's scroll bar
  was only drawn: now the arrows scroll one line, a track click scrolls a
  page, and the thumb can be dragged (arrows and track repeat while held).
  The mouse wheel now also works on fine-step wheels and touchpads, which
  used to scroll nothing. Briefing, objectives and story.
- **Scroll bars and the wheel in every text list of the menus.** Tutorials,
  the help (topics and text, which now has a bar), scores and chapters now
  scroll with the wheel (the list under the cursor) and the bar (arrows, track
  and dragging), like the original's Windows lists. The chapter list cut off
  the last chapters from mission 12 on, with no way to scroll.
- **Scrolling the map up and down from the screen edge.** In a maximized
  window, moving the cursor to the top or bottom edge barely scrolled the map
  (sideways was fine): the cursor slipped onto the title bar or the taskbar,
  left the window, and scrolling stopped. As in the original, a cursor beyond
  the edge now keeps scrolling.
- **Eden mission 05: "Have 3 or more empty Cargo Trucks".** This objective
  never completed, even with empty trucks in the colony: counting trucks by
  cargo type had become a count of the amount carried (for partial loads),
  and an empty truck carries zero. "Empty" counts trucks again. All 22
  objectives of the mission were checked one by one — including the satellite
  launch — up to MISSION ACCOMPLISHED.

**Checked**

- **What every research unlocks.** An audit walked the whole research tree,
  Eden and Plymouth. Every research unlocks its buildings and vehicles only
  once completed, and applies every improvement it promises. The recycling
  chain was tested end to end:
  - Metals Reclamation unlocks the GORF kit;
  - with the GORF running, the empty truck gets the wreckage-collecting
    command (SALVAGE);
  - Recycler Postprocessing raises the metal recovered from 50% to 60%.

  No problem found.

**New**

- **Scroll bars in the in-game lists** (the **List Scroll Bars** option, in the
  preferences and the Options tab; or `ListScrollBars=1` in the `[Game]` section
  of `OUTPOST2.INI`). In the original, in-game lists only have page arrows.
  With the option on, each gets a draggable bar instead of the arrows, and the
  mouse wheel scrolls the list under the cursor: reports (factories, labs,
  resources, communications, spaceport), options, objectives, research,
  production and the vehicle list. With it, the mouse wheel also **scrolls the
  map** (2 tiles per notch; a touchpad's sideways swipe, sideways). In the
  preferences screen it is called **Wheel/Scroll Bars**. Off (the default),
  it is the original game.

- **The `--harness` mode runs nearly all OP2TestHarness scripts.**
  [OP2TestHarness](https://github.com/leviathan400/OP2TestHarness) plays a
  scenario in the original game and records what happened; `--harness` plays
  the same script in the port, so the two can be compared. The port knew 15
  verbs, and 89 of op2-rs's 478 scenarios stopped at verbs it didn't have.
  It now has the economy and colony verbs (`beacon`, `tube`, `wall`, `player`,
  `tech`, `mem`, `poke`, `route`, `produce`, `build`, `kit`, `bay`, `cargo`,
  `transfercargo`, `load`/`unload`, `idle`/`unidle`, `train`, `research`,
  `launch`), the earthworks (`doze`, `buildwall`, `removewall`), the targeted
  orders (`repairobj`, `reprogram`, `dismantle`, `scatter`), the disasters
  (`meteor`, `quake`, `eruption`, `lightning`, `tornado`, `blight`,
  `empmissile`, `lavaspeed`, `lavapossible`, with `now` to skip the warnings)
  and the missions' AI groups (`group`). Each sends the packet, or makes the
  mission API call, the harness makes in the original. Only the mouse and
  keyboard verbs (`click`, `drag`) are left: there they are real input to the
  game's window. Verbs the harness itself doesn't know (`salvage`,
  `groupmove`) get the same "unknown verb" as there.

- **`--harness` writes telemetry (`telemetry.jsonl`)**, as OP2TestHarness
  does in the original: with `--telemetry-every N`, one line at tick 1, every
  N ticks and at the end, with each player's colony (food, ore, population,
  morale, power, buildings, techs) and every building and vehicle (type,
  owner, tile, HP, command). With `--eco`, also the colony's allocation (free
  and required workers and scientists, power, food, capacities, the morale
  inputs), each unit's state (flags, action and its timer, a truck's cargo,
  the dock) and the mining beacons. The field names and coordinates are the
  original's, so the two can be compared field by field (167 of op2-rs's
  scenarios record `--eco` telemetry). Fields the port doesn't model are left
  out rather than written as 0. And the script now runs in tick order, as in
  the harness: a tick-1 line listed after lines for later ticks waited for
  them (in `chaos_big_colony` the colonists only arrived at tick 17085).

- **`--harness`'s trace (`trace.jsonl`) has all of OP2TestHarness's
  fields.** Besides position, direction, HP and weapon, each unit now has its
  command and action (by the original's names), the route it's on (the
  destination, the step, the pixel fraction, the speed, the waypoints and the
  directions left), its EMP, foam and ESG effects and the tiles it holds. Each
  line also has the scenario units' weapon fire, every other live object
  (explosions, animations, units the script didn't create), the object
  allocator (the highest index and the ring of free slots) and the dead units
  whose index now belongs to another. The first line is at tick 1, as in the
  harness, and the `trace` verb writes one on the spot. The nine reference
  scenarios are unchanged; in the duels every new field matches the original
  on every tick.

**For those following the code:** sections 5.48 to 5.52 of PORT_STATUS; the
`--brieftest`, `--listscrolltest`, `--panescrolltest`, `--missionscrolltest`,
`--e05test`, `--objdump`, `--techunlocktest` and `--e05objtest` test modes;
sections up to 5.54.

---

## 2026-11-19

### Português

**Corrigido**

- **Os caminhões de minério não travam mais na doca.** Com vários caminhões
  na mesma rota (uns 10 já bastavam), o que acabava de descarregar na
  fundição ficava cercado pelos carregados que esperavam a vez: ele não
  conseguia sair, eles não entravam, e o transporte parava de vez. Agora os
  que esperam saem da frente — ou trocam de lugar com o que sai, quando estão
  apertados demais. Junto, outro defeito que vinha do original: um caminhão
  desviado às vezes **abandonava a rota** e parava carregado no meio do
  caminho; isso também acabou (e a patrulha tinha o mesmo problema). Testado
  com até 25 caminhões numa só fundição.
- **O cursor de ataque (e os outros) como no original.** Com unidades
  selecionadas e nenhum comando escolhido, o cursor mostra o que o clique vai
  fazer: sobre um inimigo, o de **ataque**; sobre o chão, o de mover; sobre a
  doca de um prédio seu, o de atracar; com um Spider sobre um inimigo
  paralisado, o de reprogramar; sobre uma unidade sua, a seta. E o clique
  faz exatamente isso — sobre o inimigo ele agora **ataca** (antes só movia
  até ele).
- **O mouse como no original: o botão esquerdo faz tudo.** No original, com
  unidades selecionadas, o clique **esquerdo** dá a ordem (move, ataca,
  atraca, reprograma — o que o cursor mostrar) e, numa unidade sua, a
  seleciona; o **direito** desseleciona e cancela o comando escolhido (para
  traçar uma caixa nova com unidades selecionadas, desseleciona-se antes com
  o direito). O porte
  usava o esquema dos RTS modernos, herdado do fork (o esquerdo seleciona, o
  direito dá a ordem); agora o padrão é o do original, e o outro esquema virou
  a opção **Right-Click Orders** (preferências do menu e aba Options; ou
  `RightClickOrders=1` na seção `[Game]` do `OUTPOST2.INI`).
- **Todas as opções do porte vêm desligadas.** O padrão é sempre o
  comportamento do original, e cada evolução do porte é uma opção que se liga
  no `GAME PREFERENCES`. Duas vinham ligadas e agora vêm desligadas: a rota de
  minério num clique (**Auto Ore Route**) e a câmera que passeia no demo de
  atração (**Demo Camera**). Quem já gravou um valor nas preferências continua
  com ele.
- **O botão da rota de minério de três cliques mostra o cursor.** Ao apertar o
  botão (o das duas setas, `S&et an ore-hauling route`), o cursor ficava a
  seta, e parecia que o botão não tinha armado. Os cliques até funcionavam, mas
  só valem exatamente na doca, e um clique fora era recusado sem aviso. Agora,
  como no original, o cursor é o de **atracar** sobre a doca que vale naquela
  etapa (a da mina, depois a da fundição, depois a da mina de novo) e o de "não
  atracar" no resto.
- **Missão 04 do Eden: o Scout transmite os dados do laboratório.** Mandado ao
  Advanced Laboratory com um clique em cima do prédio, o Scout parava ao lado
  dele e ficava tentando chegar à casa ocupada para sempre. Como nunca
  "chegava", a transmissão não começava e o objetivo não podia ser cumprido.
  Agora, encostado no prédio do destino, a unidade chega, e o Scout transmite.
  Vale para qualquer veículo mandado para cima de um prédio. (A comparação com
  o código original mostrou que lá o defeito é o mesmo; aqui ele foi
  corrigido, e duas pequenas diferenças da transmissão foram alinhadas ao
  original.)

- **A noite como no original: a luz das unidades.** No original, à noite,
  cada veículo com os faróis acesos e cada prédio com energia abre um círculo
  de luz no escuro, do tamanho do alcance de visão dele, com a borda
  pontilhada; por onde o veículo passa o escuro se abre, e volta quando ele
  sai. O porte deixava tudo na mesma escuridão. Agora a luz existe, e os
  prédios e veículos fora dela escurecem — os do inimigo mais que os seus e
  os dos aliados —, como lá (antes as unidades nunca escureciam, por causa de
  uma leitura errada do código original).
- **A noite agora vem escura como no original.** A opção `Night Darkness`
  passou a vir em **100%** (era 75%, uma noite mais clara que o porte usava
  porque não havia a luz das unidades). Quem já gravou outro valor nas
  preferências continua com ele.

**Novo**

- **Fila de produção** (opção **Production Queue**, nas preferências do menu e
  na aba Options durante a partida; ou `ProductionQueue=1` na seção `[Game]` do
  `OUTPOST2.INI`). No original a fábrica produz um item por ordem: escolher,
  `BUILD IT`, esperar, e repetir. Com a opção ligada:
  - cada clique num item da lista soma **+1** ao pedido, e o número aparece na
    frente do item; o clique direito tira 1; dá para misturar itens, e eles
    saem na ordem dos cliques;
  - o clique que o minério não cobre — contando o que já está nas filas de
    todas as fábricas — não soma, com o som de "sem recursos" do jogo;
  - o **`BUILD IT`** manda o pedido para a fila, e a fábrica produz um item
    atrás do outro sozinha, cada um quando houver minério, baia livre e
    operários;
  - enquanto produz, a lista continua aberta para somar mais, com o item atual,
    a porcentagem e uma barra de progresso acima dos botões, e o quadro de
    status mostra quantos estão na fila e qual é o próximo;
  - o **`ST&OP`** para o item atual e cancela o resto da fila.

  A fila é só da interface: a partida em rede funciona como sempre, mas ela não
  é gravada no jogo salvo. Desligada (o padrão), é o jogo original.

**Para quem acompanha o código:** seções 5.40 a 5.47 do PORT_STATUS; os modos
de teste `--prodqueuetest`, `--lighttest`, `--truckjamtest` e `--e04test` (e o `--cursortest`
ampliado); os módulos
`src/game/production_queue.cpp` e `src/game/light_map.cpp`.

### English

**Fixed**

- **Ore trucks no longer jam at the dock.** With several trucks on the same
  route (about 10 was enough), the one that had just unloaded at the smelter
  got boxed in by the loaded ones waiting their turn: it could not leave, they
  could not get in, and hauling stopped for good. Now the waiting trucks get
  out of the way — or swap places with the one leaving, when it is too
  crowded. Along with it, another defect inherited from the original: a
  truck that had been pushed aside sometimes **dropped its route** and stopped,
  loaded, halfway; that is gone too (and patrols had the same problem). Tested
  with up to 25 trucks on a single smelter.
- **The attack cursor (and the others) as in the original.** With units
  selected and no command chosen, the cursor shows what the click will do:
  over an enemy, the **attack** cursor; over the ground, move; over one of your
  buildings' dock, dock; with a Spider over a paralysed enemy, reprogram; over
  one of your own units, the arrow. And the click does exactly that — over an
  enemy it now **attacks** (it used to just move to it).
- **The mouse as in the original: the left button does it all.** In the
  original, with units selected, the **left** click gives the order (move,
  attack, dock, reprogram — whatever the cursor shows) and, on one of your
  units, selects it; the **right** button clears the selection and cancels
  the chosen command (to drag a new box while units are selected, clear the
  selection with the right button first). The port used the modern RTS scheme inherited from the
  fork (left selects, right orders); the original's is now the default, and
  the other scheme became the **Right-Click Orders** option (main menu
  preferences and the Options tab; or `RightClickOrders=1` in the `[Game]`
  section of `OUTPOST2.INI`).
- **Every port option now defaults to off.** The default is always the
  original's behaviour, and each of the port's additions is an option to
  switch on in `GAME PREFERENCES`. Two used to default to on and now default
  to off: the one-click ore route (**Auto Ore Route**) and the touring camera
  of the attract demo (**Demo Camera**). Anyone who already saved a value in
  the preferences keeps it.
- **The three-click ore route button shows its cursor.** After pressing the
  button (the two-arrow one, `S&et an ore-hauling route`), the cursor stayed
  the arrow, and the button seemed not to have armed. The clicks did work, but
  they only count exactly on the dock, and a click elsewhere was refused with
  no visual hint. Now, as in the original, the cursor is the **dock** cursor
  over the dock that counts at that step (the mine's, then the smelter's, then
  the mine's again) and the "no dock" cursor elsewhere.
- **Eden mission 04: the Scout transmits the lab's data.** Sent to the Advanced
  Laboratory by clicking on top of the building, the Scout stopped next to it
  and kept trying to reach the occupied cell forever. Since it never
  "arrived", the transmission never started and the objective could not be
  met. Now, touching the destination building, the unit arrives, and the Scout
  transmits. This applies to any vehicle sent on top of a building. (The
  comparison with the original code showed the defect is the same there; here
  it was fixed, and two small differences in the transmission were aligned
  with the original.)
- **Night as in the original: the units' light.** In the original, at night,
  every vehicle with its headlights on and every powered building opens a
  circle of light in the dark, as large as its sight range, with a dotted
  edge; where a vehicle goes the dark opens, and it closes again when it
  leaves. The port left everything equally dark. The light is there now, and
  buildings and vehicles outside it darken — the enemy's more than yours and
  your allies' —, as there (units used to never darken, because of a misread
  of the original code).
- **Night is now as dark as in the original.** The `Night Darkness` option now
  defaults to **100%** (it was 75%, a lighter night the port used because the
  units' light was missing). Anyone who already saved another value in the
  preferences keeps it.

**New**

- **Production queue** (the **Production Queue** option, in the main menu's
  preferences and in the Options tab during a game; or `ProductionQueue=1` in
  the `[Game]` section of `OUTPOST2.INI`). In the original a factory produces
  one item per order: pick it, `BUILD IT`, wait, and repeat. With the option on:
  - each click on an item in the list adds **+1** to the order, and the count
    shows in front of the item; right-click removes 1; items can be mixed, and
    they come out in the order of the clicks;
  - a click the ore cannot cover — counting what is already in the queues of
    every factory — does not add, with the game's "insufficient resources"
    sound;
  - **`BUILD IT`** sends the order to the queue, and the factory produces one
    item after another on its own, each one when there is ore, a free bay and
    workers;
  - while it produces, the list stays open to add more, with the current item,
    its percentage and a progress bar above the buttons, and the status pane
    shows how many are queued and which one is next;
  - **`ST&OP`** stops the current item and cancels the rest of the queue.

  The queue lives in the interface only: network games work as always, but it
  is not stored in saved games. Off (the default), it is the original game.

**For those following the code:** sections 5.40 to 5.47 of PORT_STATUS; the
`--prodqueuetest`, `--lighttest`, `--truckjamtest` and `--e04test` test modes (and the
extended `--cursortest`); the
`src/game/production_queue.cpp` and `src/game/light_map.cpp` modules.

---

## 2026-11-18

### Português

**Novo**

- **Diagnóstico de comando recusado** (`CommandDebug=1` na seção `[Game]` do
  `OUTPOST2.INI`). Quando um prédio não aceita a ordem — produzir, pesquisar,
  treinar —, aparece no registro de mensagens o motivo: falta de
  trabalhadores, de cientistas, de energia, de minério (e quanto), prédio já
  ocupado, tópico já pesquisado... O original não diz nada. Vem desligado, e
  também se liga pela caixa **Command Debug** do `GAME PREFERENCES` do menu ou
  pela linha de mesmo nome no `Game Preferences` da aba Options, durante a
  partida.

**Corrigido**

- **A pesquisa e a produção não falham mais "ao acaso".** Mandar pesquisar no
  laboratório, ou produzir um veículo ou um kit na fábrica, às vezes só pegava
  na 3ª, 4ª, até 10ª tentativa. A proteção contra clique repetido (que existe
  no original) guardava os registros para sempre: depois de um tempo, quase
  todo comando novo para a mesma fábrica ou o mesmo laboratório parecia
  "repetido" e era descartado em silêncio. Agora cada registro vale só pelos
  poucos instantes em que deve valer. A proteção contra o clique duplo continua.
- **A praga aparece no minimapa.** O micróbio se espalhava pelo mapa, mas o
  minimapa não mostrava nada. Agora, como no original, cada pedaço infectado
  fica roxo no minimapa, e dá para acompanhar a mancha surgir e crescer.
  Diferente do original, quando a praga é apagada (pela lava, por exemplo) o
  roxo some junto — lá ele ficava até trocar a vista tática.
- **O minimapa com as cores do original.** As unidades de cada jogador
  aparecem nas cores puras do original (azul, vermelho, verde…), no lugar dos
  tons pastel de antes; e os mining beacons ainda sem mina e os destroços já
  encontrados aparecem em branco, o que antes faltava. O beacon de minério raro
  só aparece depois da pesquisa que permite a mina de minério raro.
- **O quadro do ConVec mostra o kit que ele leva.** Ao selecionar um ConVec,
  o quadro de informações não dizia nada sobre a carga — só o botão de
  construir denunciava que havia um kit. Agora aparece, como no original,
  "Cargo" e o nome do kit (ou "(empty)"); o caminhão mostra a carga e a
  quantidade, os tanques as melhorias da arma, e o Evacuation Transport os
  colonos.
- **A doca da Structure Factory abre.** Quando o ConVec guarda ou pega um kit
  na fábrica, faltava a animação: a porta da doca abrindo e o kit descendo para
  dentro e voltando. Agora ela aparece, com a duração do original e os sons da
  garra e da porta, que antes não tocavam.
- **A mina não trava mais depois de um descarte na doca.** Um caminhão que
  descartava a carga (botão DUMP) atracado na mina ficava preso na doca, e a
  mina não carregava mais nenhum outro caminhão.
- **A missão 2 do Eden agora termina.** As pesquisas pedidas nos objetivos
  ("Reconstruct … database") não saíam da lista, mesmo concluídas no
  laboratório, e a missão nunca acabava. O gatilho de pesquisa disparava no
  primeiro instante da missão, antes de qualquer pesquisa, e era desligado em
  seguida. Agora só dispara quando a tecnologia é de fato pesquisada, como no
  original — vale para todas as missões com objetivo de pesquisa.
- **A praga (Blight) aparece.** Ela se espalhava, mas não era desenhada, então
  parecia não existir nem crescer. Agora aparece como no original: um xadrez
  roxo sobre o terreno infectado, com a opção *Display Computer Overlay* ligada.
  As bordas usam o arquivo `VIRMASK.RAW` do jogo; sem ele, saem mais quadradas.
- **Os veículos de trabalho mostram a animação de trabalho**, como no original:
  o Earthworker assentando tubo ou muro, o ConVec construindo ou reparando, a
  Spider e o veículo de reparo reparando, e o Robo-Dozer terraplanando. Antes
  eles ficavam com a animação de andar girando, parados no lugar.
- **O tubo ligado à doca de um prédio.** Um tubo que terminava na doca (a casa
  onde o caminhão atraca) às vezes ligava o prédio e às vezes não, conforme a
  ordem dos prédios — o tubo parecia pronto e o prédio continuava desligado.
  Agora é sempre como no original: **a doca não liga**; o tubo tem de encostar
  numa parte do prédio que não seja a doca.

**Para quem acompanha o código:** seções 5.34 a 5.39 do PORT_STATUS; os modos de
teste `--tubetest`, `--e02test`, `--dumptest`, `--dumpmission`, `--vehpanetest`, `--convecdocktest`, `--minimaptest` e `--cmddebugtest`; o gerador `tools/extract_anim_tables.py` extrai os quatro
conjuntos de animação dos veículos.

### English

**New**

- **Refused-command diagnostics** (`CommandDebug=1` in the `[Game]` section of
  `OUTPOST2.INI`). When a building does not accept an order — produce,
  research, train — the message log shows why: not enough workers, scientists
  or power, not enough ore (and how much), building already busy, topic
  already researched... The original says nothing. Off by default; it can
  also be turned on with the **Command Debug** check box in the main menu's
  `GAME PREFERENCES`, or the line of the same name in the Options tab's `Game
  Preferences`, during a game.

**Fixed**

- **Research and production no longer fail "at random".** Ordering research
  at a lab, or a vehicle or kit at a factory, sometimes only took on the 3rd,
  4th, even 10th try. The protection against repeated clicks (which the
  original has) kept its entries forever: after a while almost every new
  command to the same factory or lab looked "repeated" and was silently
  dropped. Each entry now only lasts the few moments it is meant to. The
  double-click protection is still there.
- **The Blight shows on the minimap.** The microbe spread across the map, but
  the minimap showed nothing. Now, as in the original, every infected spot turns
  purple on the minimap, so you can watch the patch appear and grow. Unlike the
  original, when the Blight is wiped out (by lava, for example) the purple goes
  with it — there it stayed until the tactical view was toggled.
- **The minimap uses the original's colours.** Each player's units show in the
  original's pure colours (blue, red, green…) instead of the pastel tones used
  before; and mining beacons with no mine yet and wreckage already found show in
  white, which was missing. A rare ore beacon only shows after the research that
  allows the rare ore mine.
- **The ConVec info pane shows the kit it carries.** Selecting a ConVec showed
  nothing about its cargo — only the build button gave away that it had a kit.
  Now, as in the original, it shows "Cargo" and the kit name (or "(empty)");
  the truck shows its cargo and amount, tanks their weapon upgrades, and the
  Evacuation Transport its colonists.
- **The Structure Factory dock opens.** When a ConVec stored or picked up a kit
  at the factory, the animation was missing: the dock door opening and the kit
  going down inside and back up. It now plays, with the original's timing and
  the claw and door sounds, which did not play before.
- **A mine no longer jams after a dump at its dock.** A truck that dumped its
  cargo (DUMP button) while docked at a mine got stuck on the dock, and the mine
  loaded no other truck.
- **Eden mission 2 can now be won.** The research asked for in the objectives
  ("Reconstruct … database") never left the list, even when finished in the
  lab, and the mission never ended. The research trigger fired at the very
  first moment of the mission, before any research, and was then switched off.
  It now fires only once the technology is actually researched, as in the
  original — this applies to every mission with a research objective.
- **The Blight shows up.** It spread but was never drawn, so it seemed not to
  exist or grow. It now looks as in the original: a purple checkerboard over
  infected terrain, with the *Display Computer Overlay* option on. The edges use
  the game's `VIRMASK.RAW` file; without it they come out squarer.
- **Work vehicles show their work animation**, as in the original: the
  Earthworker laying tube or wall, the ConVec building or repairing, the Spider
  and the repair vehicle repairing, and the Robo-Dozer bulldozing. They used to
  spin their driving animation while standing still.
- **A tube connected at a building's dock.** A tube ending at the dock (the
  tile where trucks dock) sometimes connected the building and sometimes did
  not, depending on the order of the buildings — the tube looked finished and
  the building stayed off. It now always matches the original: **the dock does
  not connect**; the tube has to touch a part of the building that is not the
  dock.

**For those following the code:** sections 5.34 to 5.39 of PORT_STATUS; the
`--tubetest`, `--e02test`, `--dumptest`, `--dumpmission`, `--vehpanetest`, `--convecdocktest`, `--minimaptest` and `--cmddebugtest` test modes; the `tools/extract_anim_tables.py` generator extracts
the four animation sets of the vehicles.

---

## 2026-11-17

### Português

**Novo**

- **Pontos de passagem com Ctrl + clique**, como no original: depois de
  apertar um comando — mover, ficar parado, atacar, guardar, construir,
  reparar, patrulhar, rota de minério —, cada **Ctrl + clique** no mapa marca
  um ponto, desenhado como um quadrado vermelho numerado, e a unidade passa por
  eles, na ordem, antes de ir ao alvo. Até 7 pontos na maioria dos comandos (6
  na patrulha, 5 na rota de minério). Cheio, o último ponto é trocado.
- **Shift na seleção**, como no original: **Shift + clique** num veículo põe
  ele na seleção, ou tira se já estava; o **botão do meio** do mouse faz o
  mesmo. **Shift + caixa** soma os veículos à seleção — a caixa sem Shift
  continua substituindo, como sempre foi neste porte. Um veículo atordoado por
  EMP não entra na seleção, como no original.
- **O bipe ao fechar o jogo**, como no original: ao sair, a janela some, toca
  um bipe e o jogo espera ele acabar. Com o som desligado, fecha na hora — o
  original esperava 3,5 s à toa.
- **As opções do porte se trocam pelo jogo**, sem editar o INI. O `GAME
  PREFERENCES` do menu principal ficou mais largo e ganhou uma coluna *"Port
  extras"* para tudo o que é acréscimo deste porte: **Auto Ore Route** (a rota
  de minério num clique), **Wrap All Maps** (todo mapa dá a volta), **Show
  Bottom Row** (mostra a última linha do mapa), **Demo Camera** (a câmera
  passeia no demo), **Original Menu Skin** (a pele de diálogo do original —
  agora troca na hora, sem reabrir o jogo) e a barra **Night Darkness** (quanto
  as noites escurecem). Desmarcada, cada caixa é o comportamento do original.
  Dentro da partida, a lista *Game Preferences* da aba Options ganhou **Audio
  Volume**, **Night Darkness**, **Wrap All Maps**, **Show Bottom Row** e
  **Auto Ore Route**, que valem no `&APPLY` sem reiniciar a partida.

**Corrigido**

- **Veículos em grupo não ficam mais andando sem parar.** Mandando muitos
  veículos juntos para o mesmo lugar, alguns ficavam para sempre tentando
  chegar. O veículo empurrado agora anda até a casa ao lado como no original
  (e, se ela estiver ocupada, empurra ou espera a sua vez), e quem não acha
  caminho espera um pouco mais a cada tentativa, em vez de tentar a cada
  instante.
- **A última linha do mapa fica escondida**, como no original: a vista para no
  fundo da área jogável do mapa, então a última linha de tiles nunca aparece.
  `ShowBottomRow=1` na seção `[Game]` do `OUTPOST2.INI` deixa a vista descer
  até ela.
- **O piso da Garagem aparece**, nas duas facções: o piso cinza com as setas
  amarelas na garagem de Plymouth, e o chão escuro com luzes na de Eden — antes
  o terreno aparecia por dentro do prédio.

**Para quem acompanha o código:** seções 5.28 a 5.33 do PORT_STATUS; os modos
de teste `--stacktest` (que veio do PR #2), `--exitbeeptest` e `--waypointtest`; casos novos no `--scrolltest`, no
`--drawordertest`, no `--uitest`, no `--optmenutest` e no `--selecttest`.

### English

**New**

- **Ctrl+click waypoints**, as in the original: after pressing a command —
  move, stand ground, attack, guard, build, repair, patrol, ore route —, each
  **Ctrl+click** on the map marks a point, drawn as a numbered red square, and
  the unit goes through them, in order, before heading to the target. Up to 7
  points for most commands (6 for patrol, 5 for the ore route). When full, the
  last point is replaced.
- **Shift in selection**, as in the original: **Shift+click** on a vehicle
  adds it to the selection, or removes it if it was already there; the
  **middle mouse button** does the same. **Shift+box** adds the vehicles to the
  selection — a box without Shift still replaces it, as it always has in this
  port. A vehicle stunned by EMP is not selected, as in the original.
- **The beep when closing the game**, as in the original: on exit the window
  disappears, a beep plays and the game waits for it to finish. With sound
  off it closes at once — the original waited 3.5 s for nothing.
- **The port's options can be changed in game**, without editing the INI.
  The main menu's `GAME PREFERENCES` is wider and has a new *"Port extras"*
  column for everything this port adds: **Auto Ore Route** (the one-click ore
  route), **Wrap All Maps** (every map wraps around), **Show Bottom Row** (the
  map's last row is shown), **Demo Camera** (the camera tours the demo),
  **Original Menu Skin** (the original dialog skin — it now switches right
  away, without restarting the game) and the **Night Darkness** slider (how
  dark the nights get). Unchecked, each box behaves as the original. During a
  mission, the Options tab's *Game Preferences* list gained **Audio Volume**,
  **Night Darkness**, **Wrap All Maps**, **Show Bottom Row** and **Auto Ore
  Route**, which take effect on `&APPLY` without restarting the mission.

**Fixed**

- **Vehicles in a group no longer keep moving forever.** Sending many vehicles
  to the same place left some of them trying to arrive forever. A pushed
  vehicle now walks to the side tile as in the original (and, if that tile is
  taken, pushes or waits its turn), and a unit that finds no path waits a
  little longer after each try instead of retrying every instant.
- **The map's bottom row is hidden**, as in the original: the view stops at
  the bottom of the map's playable area, so the last tile row is never on
  screen. `ShowBottomRow=1` in the `[Game]` section of `OUTPOST2.INI` lets the
  view scroll down to it.
- **The Garage floor is drawn**, for both factions: the grey pad with yellow
  arrows in the Plymouth garage, and the dark lit floor in the Eden one — the
  terrain used to show through the building.

**For those following the code:** sections 5.28 to 5.33 of PORT_STATUS; the
`--stacktest` (from PR #2), `--exitbeeptest` and `--waypointtest` test modes; new cases in `--scrolltest`,
`--drawordertest`, `--uitest`, `--optmenutest` and `--selecttest`.

---

## 2026-11-10

### Português

**Novo**

- **Rota de minério num clique.** Com caminhões de carga vazios selecionados,
  o **clique direito na doca de uma mina** monta o ciclo inteiro: o caminhão
  carrega, leva até a fundição e volta, sozinho, até receber outra ordem. A
  fundição escolhida é a compatível mais próxima **que esteja operando e com
  espaço**; se nenhuma estiver, fica a mais próxima de todas e o caminhão espera
  nela. Vale para minério comum, minério raro e Magma Well, nas duas raças.
  *Isto é um acréscimo deste porte — não existe no original.* O comando de três
  cliques continua igual, e `AutoOreRoute=0` na seção `[Game]` do
  `OUTPOST2.INI` desliga o atalho.

**Corrigido**

- **Quem vem de frente dá passagem.** Dois veículos que se encontram de frente
  num caminho estreito não ficam mais esperando um pelo outro: o que está entre
  dois passos recebe um desvio para uma casa ao lado, espera ali um instante e
  retoma o caminho, como no original. Antes o porte deixava os dois parados até
  desistirem e procurarem outra rota. De quebra, um defeito do original
  corrigido: se a casa do desvio fica bloqueada, o veículo não faz mais uma
  parada à toa no ponto seguinte do caminho.
- **O veículo parado sai da frente.** Um caminhão — ou qualquer veículo —
  parado na doca, ou no caminho de outro, agora é empurrado para uma casa
  vizinha quando o outro precisa passar, como no original. Antes o porte
  deixava os dois entrarem no mesmo tile. Um veículo ocupado (no meio da própria
  rota) ou um inimigo armado não são empurrados: quem chega espera um pouco e
  procura outro caminho.
- **A carga do caminhão não vaza mais da doca.** Na atracagem, o braço que leva
  o minério para dentro do prédio aparecia descendo por cima do terreno, e os
  funis saíam pelos lados da doca. Agora são cortados na borda do poço, como no
  original — o mesmo vale para o carregamento na mina e para o Magma Well.
- **Veículo atrás de prédio fica atrás do prédio.** O porte desenhava todos os
  prédios e depois todos os veículos, então um veículo passando ao norte de um
  prédio alto aparecia por cima dele. Agora a ordem é a do original: prédios e
  veículos juntos, de cima para baixo no mapa; tiros e explosões por cima; e os
  desastres (raio, tornado, meteoro) por último.
- **Inimigos armados e desarmados no caminho.** A regra de quem pode ser
  empurrado estava invertida para unidades inimigas: o porte deixava o inimigo
  **armado** ser atravessado no planejamento de rotas e bloqueava o
  **desarmado**. Agora é como no original — o armado bloqueia, o desarmado pode
  ser empurrado. Isso também muda, um pouco, como as unidades contornam
  inimigos.

**Para quem acompanha o código:** seções 5.23 a 5.27 do PORT_STATUS; cinco
modos de teste novos — `--autoroutetest`, `--pushtest`, `--artcliptest`,
`--drawordertest` e `--detourtest`.

### English

**New**

- **One-click ore route.** With empty cargo trucks selected, **right-clicking a
  mine's dock** builds the whole cycle: the truck loads, hauls to the smelter
  and comes back on its own until it gets another order. It picks the nearest
  compatible smelter **that is running and has room**; if none is, it takes the
  nearest one anyway and waits there. Works for common ore, rare ore and the
  Magma Well, for both factions. *This is an addition of this port — it is not
  in the original.* The three-click command is unchanged, and `AutoOreRoute=0`
  in the `[Game]` section of `OUTPOST2.INI` turns the shortcut off.

**Fixed**

- **Whoever comes head-on gives way.** Two vehicles meeting head-on in a
  narrow passage no longer just wait for each other: the one between two steps
  gets a detour to a side tile, waits there a moment and carries on, as in the
  original. The port used to leave both standing until they gave up and looked
  for another route. Also, a defect of the original fixed: if the detour tile
  gets blocked, the vehicle no longer makes a pointless stop at the next point
  of its path.
- **A parked vehicle gets out of the way.** A truck — or any vehicle — parked
  on a dock, or in another unit's path, is now pushed to a neighbouring tile
  when the other one needs to pass, as in the original. The port used to let
  both drive onto the same tile. A busy vehicle (mid-route) or an armed enemy is
  not pushed: the one arriving waits a little and finds another way.
- **The truck's load no longer pokes out of the dock.** While docking, the arm
  carrying the ore into the building was drawn sliding down over the terrain,
  and the hoppers stuck out of the sides of the bay. They are now clipped at the
  edge of the pit, as in the original — the same applies to loading at the mine
  and to the Magma Well.
- **A vehicle behind a building stays behind it.** The port drew every building
  and then every vehicle, so a vehicle passing north of a tall building was
  drawn over it. The order is now the original's: buildings and vehicles
  together, top to bottom of the map; shots and explosions on top; and disasters
  (lightning, tornado, meteor) last.
- **Armed and unarmed enemies in the way.** The rule for who can be pushed was
  inverted for enemy units: the port let route planning pass through an
  **armed** enemy and blocked an **unarmed** one. It now matches the original —
  armed blocks, unarmed can be pushed. This also slightly changes how units
  route around enemies.

**For those following the code:** sections 5.23 to 5.27 of PORT_STATUS; five
new test modes — `--autoroutetest`, `--pushtest`, `--artcliptest`,
`--drawordertest` and `--detourtest`.
