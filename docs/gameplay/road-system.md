# Road System Concepts

Roads are the player's transport network.  They connect buildings, flags, and
warehouses so wares and workers can move through the economy.  The game treats
road construction, road storage, and transport routing as related but separate
systems: a road can be buildable on the terrain, stored as part of the map, and
then used by wares only if the resulting network has the right carriers and
warehouse access.

## Map Roads and Road Nodes

The visible road on the map is made from edges between neighbouring map points.
Each edge has a road type such as a normal land road, donkey road, or waterway.
The economy does not route over every edge independently.  Instead, it sees a
graph of road nodes connected by road segments.

Road nodes are the places where transport decisions can change direction:

- flags,
- buildings and building sites through their front flag connection,
- harbor buildings, which can also connect to shipping routes.

A road segment is the stretch between two road nodes.  It stores the ordered
directions from one endpoint to the other, its length, its road type, and the
workers assigned to carry goods along it.  The endpoints each remember the road
segment in the direction where it leaves the node.

This means a long visible road without intermediate flags is one transport
segment.  Wares do not stop or change route in the middle of it.  Adding a flag
on that road splits the segment into two shorter segments and creates a new
routing node.

## Flags

Flags are the main junctions of the road system.  They have three jobs:

- they are road endpoints and intersections,
- they temporarily hold wares waiting for the next carrier,
- they let the route finder choose the next segment toward a destination.

A flag has limited ware space.  If it fills up, carriers and buildings around it
may have to wait until space becomes available.  When a ware is placed on a
flag, it already knows the next road direction it wants to take.  The matching
road segment is notified so its carrier can pick the ware up.

Flag spacing matters.  More flags create more handoff points, shorter carrier
walks, and more chances for the route finder to choose between branches.  Too
few flags create long segments where one carrier has to cover the whole stretch.
Too many flags can consume buildable space and lower nearby building quality.

## Building Entrances

Every normal building uses a front flag on the south-east neighbour of the
building position.  The game creates that flag if needed when the building or
building site is placed.  The building is then connected to its front flag by a
one-step entrance road.

That entrance road is not a normal player road segment between two flags.  It is
the final connection between the building and the road network.  Wares usually
travel through flag-to-flag road segments until they reach the front flag, then
enter the building through this short connection.

Large buildings can also occupy extension points around their main position, so
roads and flags near them can reduce building-quality options for future
construction.

## Road Types

There are three gameplay road types:

- Normal roads are land roads served by a carrier.
- Donkey roads are upgraded land roads.  They still need the normal carrier and
  can also use a donkey as the second worker on the same segment.
- Waterways are water road segments served by a carrier with a boat.

Donkey roads upgrade the visual road and the endpoint flags.  They are useful on
busy land segments because the extra worker can move more goods over the same
connection.  Waterways belong to the transport graph too, but not every kind of
path search is allowed to use them.  For example, land-only movement and many
road-construction checks ignore water roads.

The maximum waterway-length addon is enforced by the game world when the road
command executes. This applies equally to UI, AI, network, and replay commands.

## Building Roads

A player-built road starts at an owned flag and follows a route of neighbouring
map directions.  The route must be long enough to form a real road segment
between flags.  The last point must either already contain an owned flag or be a
valid place where the game can create an endpoint flag.

For the intermediate points, the game requires:

- the point is inside the player's territory,
- no blocking object occupies the point,
- the point is not on a border stone,
- no existing road already uses the point,
- nearby objects that forbid roads around them are not present,
- the terrain supports the chosen road type.

Land roads need at least some surrounding terrain that can support a flag or
better, and no dangerous terrain around the point.  Waterways need water around
the point.

When the road is accepted, the game writes road edges along the route,
recalculates building quality around affected points, creates an endpoint flag
when necessary, and registers the new segment with the player's economy.

## Auto Flags and Splitting

Flags can be placed manually on an existing road.  When that happens, the old
road segment is split at the new flag.  Figures already walking on the old
segment are corrected to use the new segment data, and carriers are reassigned
as needed.

The auto-flags addon can place flags along newly built human land roads.  These
automatic flags are only placed where they do not violate the adjacent-flag
rules.  The practical effect is the same as manual splitting: long roads become
multiple shorter transport segments.

## Carriers and Workers

A road segment is useful for ware transport only when it can get the right
worker:

- a normal or donkey land segment needs a helper as the carrier,
- a waterway needs a helper and a boat,
- an upgraded donkey road can request a donkey after its normal carrier exists.

When a new road connection is built, all roads check again whether they can find
carriers.  Workplaces, building sites, lost wares, and military buildings also
recheck their logistics because the new connection may have made new routes
possible.

If a carrier loses its workplace because a road is destroyed or split, the game
updates the affected segment and tries to find a replacement where appropriate.

## Ware Routing

Ware routing searches the road-node graph for a path between the ware's current
location and its destination.  Segment length is the base cost.  In ware-routing
mode, the search can also add penalties for congestion and missing service:

- wares already waiting for the same direction add cost,
- a carrier that has been ordered but has not arrived yet adds cost,
- a segment without a required carrier adds a large cost,
- starting shipping from a harbor has its own large penalty.

Those penalties do not make a segment impossible by themselves.  They make
another available route more attractive when one exists.  This lets the economy
prefer less busy paths without requiring a global traffic optimizer.

Some path searches deliberately use a simpler model.  Human road-node walking
and AI road-connection checks can ask for plain land-road distance, which counts
segment length and excludes water roads and carrier congestion.

The AI may add waterways as ware-logistics shortcuts between shoreline flags
that are already connected by land roads. It does not use waterways to satisfy
building or worker connectivity. Completed AI waterways are kept as single
segments without automatically added interior flags, and the AI can assign a
shipyard to replenish a small stored-boat reserve.

## Road Destruction and Capture

Destroying a flag destroys its connected road segments and removes wares waiting
there.  Destroying a building removes its entrance road and any connected road
state owned by the building.  Capturing a flag destroys most roads around it
except the building entrance direction, clears wares on the flag, and changes
the flag owner.

When roads disappear, wares waiting at flags recalculate their routes.  Workers
on the removed road lose that workplace, and figures currently walking on it are
made to leave the road state.

## Building Quality Impact

Roads are not just transport infrastructure; they also consume buildable space.
A road point can reduce nearby building quality, especially for large buildings
whose extension points cannot overlap roads.  Flags have a similar effect
because adjacent flags are not allowed and because a building plot includes the
ability to place or reuse its front flag.

The result is an important tradeoff: dense road networks improve handoffs and
alternate routes, but they can also damage future construction options.

## Practical Consequences

- Roads connect the economy at flags, not at every visible road tile.
- Long roads without flags are long carrier jobs.
- Adding a flag to a road creates a new routing decision point.
- A building is connected through its front flag, so the front flag is the
  important logistics point for most production.
- A road may physically exist but still perform poorly until it has carriers,
  enough flag space, and a connected destination.
- Waterways are part of ware logistics, but many land-road and person-routing
  decisions intentionally exclude them.
