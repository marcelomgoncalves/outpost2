# Changelog

Mudanças do porte, agrupadas por data — a mais recente primeiro. O detalhe
técnico de cada uma está no [PORT_STATUS.BR.md](PORT_STATUS.BR.md).

*Changes to the port, grouped by date — most recent first. The technical
details of each one are in [PORT_STATUS.BR.md](PORT_STATUS.BR.md)
(Portuguese).*

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

**Para quem acompanha o código:** seções 5.23 a 5.26 do PORT_STATUS; quatro
modos de teste novos — `--autoroutetest`, `--pushtest`, `--artcliptest` e
`--drawordertest`.

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

**For those following the code:** sections 5.23 to 5.26 of PORT_STATUS; four
new test modes — `--autoroutetest`, `--pushtest`, `--artcliptest` and
`--drawordertest`.
