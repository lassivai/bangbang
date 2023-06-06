---- Keyboard/mouse shortcuts (until a proper GUI will be made) ----

Escape			    Closes the application
F2			        Toggles player 1 human/AI controlled
F3			        Toggles player 2 human/AI controlled
F4			        Spawns a walker (for debugging)
F5			        Spawns a robot (for debugging)
F6, F7, F12		    Prints to console debugging information you don't need
G			        Toggles debugging rendering of pixels
N			        Toggles sound effects on/off
R, T, Y, U, I, O	Changes terrain brush type (experiment if you want to, for debugging)
1, 2, 3, 4, 5, 6, 7	Changes terrain brush size (for debugging)
Mouse drag		    Paints terrain (for debugging)


---- CHARACTERS ----

Full HP: 100
Full mana: 100

Actions:

Character 1:
'move', arrow keys
'jump', key '-' 
'use primary equipment', key '.'
'use secondary equipment', key ','
'toggle equipment change menu', key 'm'
When the menu is active: left/right change primary equipment, and up/down change secondary equipment. Double clicking 'm' when near to a walker enters the walker.

Character 2:
'move', wasd
'jump', key 'left ctrl' 
'use primary equipment', key 'left shift'
'use secondary equipment', key 'tab'
'toggle equipment change menu', key 'q'
When the menu is active: a/d change primary equipment, and w/s change secondary equipment. Double clicking 'q' when near to a walker enters the walker.


---- CHARACTER PRIMARY EQUIPMENTS ----

##### Bomb ####
Explodes on collision to ground/dirt or characters.
Damage: 25 HP
Explosion radius: 100
Mana cost: 16.666
Loading time to full: 10 s

##### Cluster Bomb ####
First key press of 'use' throws the bomb, second key press launches 10 clusters.
Damage: 25 HP
Explosion radius: 40
Mana cost: 25
Loading time to full: 10 s

##### Napalm ####
First key press of 'use' throws the bomb, second key press launches the burning projectiles.
Damage: ???
Mana cost: 25
Loading time to full: 10 s

##### Flame Thrower ####
Powerful weapong when used nearby.
Damage: 1 HP / second / flame projectile
Mana cost: 30 / second
Loading time to full: 10 s

##### Lightning Strike ####
Instant hit (unlimited velocity). The longer the open distance, the more powerful the strike, because of the branching.
Damage: ???
Mana cost: 25
Loading time to full: 10 s

##### Blaster ####
The favorite weapon of many warriors in the Star Wars universe.
Damage: 10 HP
Mana cost: 5
Loading time to full: 10 s

##### Nuclear Bomb ####
Perhaps the best idea is to drop these bombs from higher ground than your opponent. First key press of 'use' drops the bomb, second key press detonates it. The explosion radius is drawn around the bomb.
Damage: 100 HP
Explosion radius: 350
Mana cost: 0 (for testing... should be around 100)
Loading time to full: 60 seconds

##### Missile Launcher ####
Speed up to high velocity by constant force.
Damage: 25 HP
Explosion radius: 150
Mana cost: 25
Loading time to full: 10 s

##### Reflector Beam ####
Instant hit (unlimited velocity). Reflects from map edges, and ground/dirt pixels.
Damage: 40 HP / second
Mana cost: 30 / second
Loading time to full: 10 s

##### Bouncy Bomb ####
Perhaps the best idea is to drop these bombs from higher ground than your opponent.
Damage: 15 HP
Eplosion radius: 75
Mana cost: 10
Loading time to full: 10 s

##### Rifle ####
Instant hit to the target (unlimited velocity).
Damage: 50 HP
Mana cost: 33.333
Loading time to full: 10 s

##### Shotgun ####
Fires 150 projectiles. Very effective when all/most of the projectiles hit the target.
Damage: 0.666 HP
Mana cost: 25
Loading time to full: 10 s

##### DoomsDay ####
Launches a projectile which bounces 30 times before final explosion, doing damage in every bounce.
Damage: 50 HP
Mana cost: 100
Loading time to full: 3 minutes

##### Bolter ####
The favorite weapon of Space Marines in Warhammer 40k universe. Fires small self-propelled missiles in fast rate.
Damage: 14.3 HP
Mana cost: 3.125
Loading time to full: 30 s
Firing rate: 10 / s

##### Railgun ####
Launches a projectile that is accelerated to high velocity without chemical means. Explosion too is based only on the high kinetic energy.
Damage: 100 HP
Mana cost: 100
Loading time to full: 30 s

##### Melter Cannon ####
Melts ground and launches it forward doing damage to anyone on the way. Similar to shotgun, most effective when the target is nearby.
Damage: ???
Mana cost: 100
Loading time to full: 5 s

##### Earthquake ####
Drops the ground to the bottom of the screen. (Effectively, changes every pixel with type 'ground' to type 'dirt'.)

##### Spawn a Walker ####
Spawns a heavily armed and armoured walker, that can be entered and controlled by the player.
Mana cost: 100
Loading time to full: 5 minutes

##### Spawn Robots ####
Spawns lightly armoured autonomous robots, that cannot be controlled by the player. Both players, not just the opposing player, better find a cover when robots are present.
Mana cost: 10
Loading time to full: 5 minutes

##### Fire Balls ####
Damage: 15 HP
Mana cost: 33.33
Loading time to full: 10 s


---- CHARACTER SECONDARY EQUIPMENTS ----

#### Digger ####
Character moving velocity is 1/3 when using this equipment.

#### Jet Pack ####
Applies a force of 1.25*gravity to up direction.

#### Rock Spawner ####
Hold the 'use' key as long as you want to spawn more stuff.

#### Wood Spawner ####
Hold the 'use' key as long as you want to spawn more stuff.

#### Repeller ####
Applies a force away from the character using this item. Doesn't work against lightning strike, blaster, reflector beam and rifle.

#### Laser Sight ####
Best used with weapons with straight trajectories (blaster, missile launcher and especially rifle)



---- WALKERS ----
Full HP: 1000
Full mana: 100
These vehicles are controlled by the players. A character needs to enter this othwise inactive vehicle. A human-controlled character can enter the walker from nearby by double-clicking the equipment change key.

---- PRIMARY EQUIPMENTS OF WALKERS ----

#### Heavy Flamer ####
A more powerful version of the common flame thrower.
Mana cost: 25 / second
Loading time to full: 10 s

#### Laser Cannon ####
A more powerful version of the common blaster.
Damage: 33.33 HP
Explosion radius: 100
Mana cost: 5
Loading time to full: 10 s
Firing rate: 5 shots/second

#### Cluster Mortar ####
Launches 10 clusters affected by gravity in to medium distance.
Damage: 20 HP
Explosion radius: 75
Mana cost: 16.666
Loading time to full: 10 s

#### Multi Bolter ####
Similar to the common bolter except fires 5 bolts simultaneously in a little bit slower rate.
Damage: 14.3 HP
Mana cost: 6.25
Loading time to full: 10 s
Firing rate: 4 shots/second

##### Heavy Melter Cannon ####
Similar to the common melter cannon except with a bigger melting range.
Melting range: 500
Mana cost: 100
Loading time to full: 5 s


---- SECONDARY EQUIPMENTS OF WALKERS ----

#### Digger ####
Walker moving velocity is 2/3 when using this equipment. Otherwise similar to the version equipped by the characters.

#### Laser Sight ####
Similar to the laser sight that is equipped by the characters.


---- ROBOTS ----
Full HP: 50
Full mana: 100
These vehicles are autonomous and are not controlled by the players.

---- PRIMARY EQUIPMENTS OF ROBOTS ----

##### Blaster ####
Damage: 10 HP
Mana cost: 5
Loading time to full: 10 s
Firing rate: 5 shots/second

---- SECONDARY EQUIPMENTS OF ROBOTS ----

#### Digger ####
Robot moving velocity is 2/3 when using this equipment. Otherwise similar to the version equipped by the characters.

#### Jet Pack ####
Applies a force of 1.25*gravity to up direction. Similar to the version used by the characters.

