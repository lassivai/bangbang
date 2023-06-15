# Bang Bang

You can download Windows binaries of Bang Bang from 
<a href="https://drive.google.com/file/d/156a0ZseVehbcP_GwQK8mlbVhbX8T4BPm/view?usp=sharing">here</a>.<br />

The soundtrack of the game can be listened in Youtube (<a href="https://www.youtube.com/watch?v=855o5PHB9P0">lower resolution</a> or <a href="https://www.youtube.com/watch?v=1jS_MG9lqrw">higher resolution</a>) or <a href="https://soundcloud.com/lassi-palmujoki/sets/bang-bang-original-soundtrack">SoundCloud</a>.<br />

# 

Version 0.??? <br />

A 2D action game inspired by 90s Finnish indie games such as Liero, Molez and Wings. The target has been old school style pixel based graphics and physics, with effects empowered by 20+ years progress in computer performance.<br />

The current version features two characters dueling against each other. The amount of characters may change in later versions. Each character can be controlled either by human player or AI. Set both characters to AI to have a realtime demo of the gameplay. The static (one-screen) map will possible be also transformed to a larger and movable map in later versions. Otherwise the map is very dynamic, each pixel of the terrain being simulated.<br />

The affairs take place in an alternative universe not much unlike the universes of Star Wars or Warhammer 40k. The weaponry has been inspired by weapons of our universe and the two universes just mentioned. Also, the characters possess inhumane capabilities that could be concidered 'magic' in our reality. Special care has been taken not to have too similar kind of weapons or other capabilities. The power of the weapons is such that the terrain tends to disappear quite quickly. To counter that, the characters have been equipped with the ability to spawn new terrain (either rock or flammable wood).<br />

In addition to moving left and right and jumping, the characters have so called 'primary equipments' and 'secondary equipments'. The main difference is that the primary equipments use mana (which recharges all the time when the mana is below 100, even if the equipment isn't active) while the secondary equipments don't. There are currently 20 primary equipments and 8 secondary equipments (plus a few more when the player is controlling a vehicle).<br />

### Credits
<ul>
<li>Game design: Lassi Palmujoki</li>
<li>Programming: Lassi Palmujoki</li>
<li>Graphics: Lassi Palmujoki (except for 3 textures, which will be painted by me in a future release)</li>
<li>Sound effects: Lassi Palmujoki (made with a soft synth made also by me, available for Linux from my github "playground")</li>
<li>Music: Lassi Palmujoki (50 minutes long original soundtrack)</li>
</ul>
  
### Known bugs and other issues
<ul>
<li>The coding style and quality is perhaps quite 'old school' too, concidering that almost all of the code base is packed in to a one source file. The main design decision has been: Easy to code for me (I mostly remember the names of the classes and can navigate around with the search tool). If the project grows or attracts other kind of attention, the code base will be perhaps divided in to one header file and one source file per class basis.</li>
<li>Textures and other graphics should be concidered 'initial'.</li>
<li>Loud sound and music might break the audio if played simultaneously. Will be fixed as soon as possible. Keeping your audio volume on a moderate level in software side might help.</li>
<li>Robots should not hit the player belonging to the same team. Your option currently is to avoid being in front of robots of either side.</li>
<li>Graphics processing unit could be used to speed things up (and have even bigger effects).</li>
<li>The difficulty level of AI can't yet be adjusted.</li>
<li>AI characters and vehicles should attack the nearest character/vehicle (not the first on the list!).</li>
<li>The game will feature a proper start menu, options menu and other graphical user inteface elements in the future.</li>
<li>Napalm flames not burning on the bottom of the screen when there is a thin layer of blood.</li>
<li>Walker collision detection could be improved (currently falls down 45 degree slopes and vibrates randomly on uneven terrain).</li>
</ul>
