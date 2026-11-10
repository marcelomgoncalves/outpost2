#pragma once

namespace op2 {

// Driver de movimento por tick. Para cada unidade com ordem de movimento ativa,
// consome a rota do pathfinder REAL (pfSearchRoute = sub_43F5B0) e desliza a
// unidade pelas células exatas dela (ocupação do grid + rotação). O algoritmo de
// BUSCA é 1:1 com o original; este executor substitui, por ora, o integrador
// byte-exato sub_44B650 (que depende do modelo completo do objeto Unit/descritor,
// ainda a mapear) — mas NÃO substitui o pathfinder: a rota é a do original.
void tickMovement();
void moveDockArrive(int id);          // ações 3/4 no destino (giro + doca)
// vt[0x7C] (sub_44B650) p/ um tile vizinho: monta o passo; false se não dá.
bool unitStepToward(int id, int tx, int ty);
// Só para o --detourtest: o empurrão (sub_44BBA0) chamado direto, com o estado
// das duas unidades montado pelo teste.
void debugPushAside(int pusherId, int dir, int blockerId);

// loc_44B8F0 de sub_44B650: a unidade do computador armada de Laser/Microwave
// que esbarra num tubo ou parede passa a mirar o TILE. Exposta porque e uma
// decisao com cinco condicoes, e o --walltest a exercita direto (fazer a
// unidade esbarrar de verdade dependeria de o pathfinder nao contornar).
void wallBlockTarget(struct UnitRecord& u, int nx, int ny);

} // namespace op2
