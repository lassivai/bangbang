/*
---- WINDOWS ----
g++ src/bangbang.cpp -IG:\programming\c++\libs\SFML-2.5.1\include -LG:\programming\c++\libs\SFML-2.5.1\lib -lsfml-graphics -lsfml-window -lsfml-system -std=c++11 --static -static-libgcc -static-libstdc++ -o bangbang.exe

g++ src/bangbang.cpp -IG:\programming\c++\libs\SFML-2.5.1\include -LG:\programming\c++\libs\SFML-2.5.1\lib -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -std=c++11 -o bangbang.exe

---- LINUX debug ----
g++ src/bangbang.cpp -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -std=c++11 -g -O0 -o bangbang

---- LINUX release ----
g++ src/bangbang.cpp -lsfml-graphics -lsfml-window -lsfml-system -lsfml-audio -std=c++11 -O3 -o bangbang

#### about optimizations with g++ ####
-Ofast: Highest "standard" optimization level. The included -ffast-math did not cause any problems in our calculations, so we decided to go for it, despite of the non standard-compliance.
-march=native: Enabling the use of all CPU specific instructions.
-flto to allow link time optimization, across different compilation units.



TODO
    - fix dirt spreading
    - fix water spreading (faster)
    - fix character-tile map collision
    - oil (ignite wood, smoke/flames at the top of burning front)
    - explosions start fire from below
    - more health to flammable tiles???

    - items/weapons/spells
        - lightning strike (max length, max branches, particle limit)
            - oil explodes incorrecly
        - items with manaCostPerSecond, load 10-20 % when mana goes to small amount before being able to use again

    - experiment with growing land
        - perhaps using fractal noise (DONE)
        - use another thread to prepare noise textures (DONE)

    - explodable projectiles
        - bounding box update before main update

    - Sounds
        - character damage
        - flame (DONE)
        - stop item sounds when dead (DONE)

    - AI
        - check max distance for reflector beam etc.
        - walker shoots friendly robots... (FIXED)
        - sort the enemies based on distance

    - Weapons
        - check if respawning or inside a vehicle... (DONE)
        - Napalm fire isn't burning on to of blood one pixel thick on the
          bottom of the screen...
        - Cluster bombs and Napalms -> detonate if weapon changes or player 
          dies

    - Textures
        - global list of loaded textures: don't load a texture more than
          once from the disk! Vehicle textures are loaded every time 
          a vehicle is spawned...

DEBUG
    - the game slows down greatly when left on for several hours. Especially
      noticeable when several robots are spawned.
    - 27.5.2023 didn't observe noticeable slowdown with 40 hours of playtime
*/


#include <cstdio>
#include <cstdlib>
#include <ctgmath>
#include <iostream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <random>
#include <algorithm>
#include <ctime>
#include <thread>
#include <mutex>
#include <SFML/Graphics.hpp>
#include <SFML/Audio.hpp>
#include <SFML/System/Vector2.hpp>
#include "noise.h"

//#include <valgrind/callgrind.h>


using namespace std;

const float Pi = 3.1415926535;

int randi(int min = 0, int max = 1) {
    float r = (float)rand() / RAND_MAX;
    return int(r * (max-min+0.9999) + min);
}

float randf(float min = 0, float max = 1) {
    float r = (float)rand() / RAND_MAX;
    return r * (max-min) + min;
}

/*std::mt19937 mt19937generatorInt;
int randi2(int min, int max) {
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(mt19937generatorInt);
}

std::mt19937 generatorFloat;
float randf2(float min, float max) {
    std::uniform_real_distribution<float> distribution(min, max);
    return distribution(generatorFloat);
}
void initializeRandomNumberGenerators() {

}*/

int randi2(int min, int max) {
    static std::mt19937 generator;
    std::uniform_int_distribution<int> distribution(min, max);
    return distribution(generator);
}

float randf2(float min, float max) {
    static std::mt19937 generator;
    std::uniform_real_distribution<float> distribution(min, max);
    return distribution(generator);
}


float lerp(float a, float b, float t) {
    if(t < 0) t = 0;
    if(t > 1) t = 1;
    return (1.0-t) * a + t * b;
}

float mapf(float x, float a, float b, float c, float d) {
    return ((x-a) / (b-a)) * (d-c) + c;
}

template<class K, class L> double min(K a, L b) {
    return a < b ? a : b;
}
template<class K, class L> double max(K a, L b) {
    return a > b ? a : b;
}

inline bool isWithinRect(float px, float py, float ax, float ay, float bx, float by) {
    return px >= ax && px <= bx && py >= ay && py <= by;
}

sf::Color mix(sf::Color a, sf::Color b, float t) {
    if(t < 0) t = 0;
    else if(t > 1) t = 1;

    return sf::Color((1.0-t) * a.r + t * b.r,
                     (1.0-t) * a.g + t * b.g,
                     (1.0-t) * a.b + t * b.b,
                     (1.0-t) * a.a + t * b.a);
}


float dotProduct(float ax, float ay, float bx, float by) {
    return ax * bx + ay * by;
}

void reflectVector(float ax, float ay, float nx, float ny, float &rx, float &ry) {
    float tmp = nx;
    nx = ny;
    ny = -tmp;
    float p = 2.0 * dotProduct(nx, ny, ax, ay);
    rx = p * nx - ax;
    ry = p * ny - ay;
}

enum class ColorTheme { Blue, Red };

struct BoundingBox {
    float ax = 0, ay = 0, bx = 0, by = 0;

    bool isPointWithin(float px, float py) {
        return px >= ax && px <= bx && py >= ay && py <= by;
    }
};

struct Mouse {
    int x, y;
    bool leftPressed = false, middlePressed = false, rightPressed = false;
};

struct Timer {
    double frameTime, fps, totalTime;
    chrono::time_point<chrono::system_clock> timeNow, timePrev, timeBeginning;

    int frames = 0;
    double t = 0;

    chrono::time_point<chrono::system_clock> timeNowTickTock, timePrevTickTock;
    double tickTockDuration = 0;

    Timer() {
        timeBeginning = std::chrono::system_clock::now();
    }

    void tick() {
        timePrevTickTock = std::chrono::system_clock::now();
    }
    double tock() {
        timeNowTickTock = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds_total = timeNowTickTock - timePrevTickTock;
        return tickTockDuration = elapsed_seconds_total.count();
    }

    void update() {
        frames++;

        timeNow = std::chrono::system_clock::now();

        std::chrono::duration<double> elapsed_seconds = timeNow - timePrev;
        t += elapsed_seconds.count();

        std::chrono::duration<double> elapsed_seconds_total = timeNow - timeBeginning;
        totalTime = elapsed_seconds_total.count();

        if(t >= 1.0) {
            frameTime = t / frames;
            fps = frames / t;
            t = 0;
            frames = 0;
        }

        timePrev = std::chrono::system_clock::now();
    }
};



float masterVolume = 1.0;


struct SoundInstance {
    sf::Sound sound;
    bool isFadingOut = false;
    float fadeOutTime = 1.0;
    float fadeOut = 1.0;
    enum RemoveType { None, Instant, FadeOut };
    RemoveType removeType = None;
    float initialVolume = 100;

    void removeWithFadeOut(float fadeOutTime = 1.0) {
        removeType = FadeOut;
        this->fadeOutTime = fadeOutTime;
    }

    void update(float dt) {
        if(removeType == RemoveType::FadeOut) {
            fadeOut -= dt / fadeOutTime;
            if(fadeOut < 0) {
                removeType = RemoveType::Instant;
            }
            else {
                sound.setVolume(masterVolume * initialVolume * pow(fadeOut, 10)); // TODO optional pow
            }
        }
    }

    void setVolume(float volume) {
        initialVolume = volume;
        sound.setVolume(initialVolume * masterVolume);
    }

    void updateVolume() {
        sound.setVolume(initialVolume * masterVolume);
    }
};


struct SoundWrapper {
    vector<SoundInstance*> soundVector;

    
    void setMasterVolume(float volume) {
        masterVolume = volume;
        for(int i=0; i<soundVector.size(); i++) {
            soundVector[i]->updateVolume();
        }
    }

    SoundInstance *playSoundBuffer(sf::SoundBuffer &soundBuffer, bool loop = false, float volume = 100) {
        SoundInstance *soundInstance = new SoundInstance();

        soundInstance->initialVolume = volume;
        soundInstance->sound.setBuffer(soundBuffer);
        soundInstance->sound.setLoop(loop);
        soundInstance->sound.setVolume(volume * masterVolume);
        soundInstance->sound.play();
        soundVector.push_back(soundInstance);
        return soundInstance;
    }

    void update(float dt) {
        for(int i=soundVector.size()-1; i>=0; i--) {
            if(soundVector[i]->sound.getStatus() == sf::SoundSource::Status::Stopped || soundVector[i]->removeType == SoundInstance::RemoveType::Instant) {
                delete soundVector[i];
                soundVector.erase(soundVector.begin()+i);
            }
        }
        for(int i=0; i<soundVector.size(); i++) {
            soundVector[i]->update(dt);
        }
    }

    void finish() {
        for(int i=soundVector.size()-1; i>=0; i--) {
            delete soundVector[i];
            soundVector.erase(soundVector.begin()+i);
        }
    }

};

struct PieceOfMusic {
    sf::Music music;
    std::string creator = "";
    std::string name = "";
    float durationSeconds = 0;

    bool openFromFile(const std::string &filename) {
        if(!music.openFromFile(filename)) {
            printf("Error at PieceOfMusic.openFromFile(): Couldn't open music '%s'!", filename.c_str());
            return false;
        }

        sf::Time t = music.getDuration();
        durationSeconds = t.asSeconds();
        return true;
    }

    void play() {
        music.play();
    }
    void pause() {
        music.pause();
    }
    void stop() {
        music.stop();
    }
    bool isPlaybackFinished() {
        return music.getStatus() == sf::SoundSource::Status::Stopped;
    }
    bool isPlaybackPaused() {
        return music.getStatus() == sf::SoundSource::Status::Paused;
    }

    float getPlayingOffsetSeconds() {
        sf::Time t = music.getPlayingOffset();
        return t.asSeconds();
    }

};

struct MusicAlbum {
    std::vector<PieceOfMusic*> musicPieces;
    PieceOfMusic *activePieceOfMusic = nullptr;
    std::default_random_engine rng = std::default_random_engine{};
    bool isPlaying = false;

    int playListIndex = 0;

    ~MusicAlbum() {
        for(int i=0; i<musicPieces.size(); i++) {
            delete musicPieces[i];
        }
    }

    void setup(double seed) {
        rng.seed(seed);
    }

    void openAlbum(const std::string &albumName, const std::string &creator, int numTracks = 0, const std::string &fileExtension = "ogg") {
        std::string path = "data/music/" + albumName + "/";
        if(numTracks > 0) {
            for(int i=0; i<numTracks; i++) {
                int index = i + 1;
                std::string filename = "track";
                if(index < 10) {
                    filename += "0";
                }
                filename += std::to_string(index);
                std::string name = filename;
                filename += "." + fileExtension;
                filename = path + filename;

                PieceOfMusic *p = new PieceOfMusic();
                p->openFromFile(filename);
                p->name = name;
                p->creator = creator;
                musicPieces.push_back(p);
            }
        }
    }

    void shufflePlayList() {
        std::shuffle(std::begin(musicPieces), std::end(musicPieces), rng);
    }

    void play() {
        isPlaying = true;
        if(activePieceOfMusic == nullptr) {
            activePieceOfMusic = musicPieces[playListIndex];
            activePieceOfMusic->play();
        }
        if(activePieceOfMusic->isPlaybackFinished() || activePieceOfMusic->isPlaybackPaused()) {
            activePieceOfMusic->stop();
            activePieceOfMusic->play();
        }
    }

    void pause() {
        isPlaying = false;
        if(activePieceOfMusic != nullptr) {
            activePieceOfMusic->pause();
        }
    }

    void update() {
        if(isPlaying) {
            if(activePieceOfMusic == nullptr) {
                activePieceOfMusic = musicPieces[playListIndex];
                activePieceOfMusic->play();
            }
            if(activePieceOfMusic->isPlaybackFinished()) {
                activePieceOfMusic->stop();
                playListIndex++;
                if(playListIndex >= musicPieces.size()) {
                    playListIndex = 0;
                }
                /*if(playListIndex >= musicPieces.size()) {
                    isPlaying = false;
                    return;
                }*/
                activePieceOfMusic = musicPieces[playListIndex];
                activePieceOfMusic->play();
            }
        }
    }

    std::string getCurrentlyPlayingPieceName() {
        std::string ret = "";
        if(activePieceOfMusic != nullptr) {
            ret += activePieceOfMusic->name;
            if(activePieceOfMusic->creator.size() > 0) {
                ret += " by " + activePieceOfMusic->creator;
            }
        }
        return ret;
    }

    float getCurrentlyPlayingPieceOffsetSeconds() {
        float ret = 0;
        if(activePieceOfMusic != nullptr) {
            ret = activePieceOfMusic->getPlayingOffsetSeconds();
        }
        return ret;
    }

    float getCurrentlyPlayingPieceDurationSeconds() {
        float ret = 0;
        if(activePieceOfMusic != nullptr) {
            ret = activePieceOfMusic->durationSeconds;
        }
        return ret;
    }
};


// Globals, these shouldn't exist

Timer timer;
float gravity = 500;
int screenW = 1200, screenH = 1200;
int scaleX = 3, scaleY = 3;
SoundWrapper soundWrapper;

bool debugRenderProjectiles = true;

bool areProjectilesPixelated = true;

int numCores = 6;



struct Map;
struct Character;
struct Vehicle;

void createSmoke(float x, float y, vector<Character*> &characters, int numSmokeProjectiles, float smokeProjectileVelocityMin, float smokeProjectileVelocityMax, float initialRadius = 5);

void createFlame(float x, float y, vector<Character*> &characters, int numFlameProjectiles, float flameProjectileVelocityMin, float flameProjectileVelocityMax, float dealDamagePerSecond);




struct Projectile {
    float x = 0, y = 0;
    float vx = 1, vy = 1;
    bool gravityHasAnEffect = true;
    bool repellerHasAnEffect = true;
    bool canBeRemoved = false;
    vector<Character*> characters;
    Character *projectileUserCharacter = nullptr;
    Vehicle *projectileUserVehicle = nullptr;
    bool initialSelfCollision = true;

    int team = 0;
    bool ignoreTeamMates = false;

    virtual ~Projectile() {}
    virtual void update(Map &map, float dt) {}
    virtual void render(sf::RenderWindow &window, float scaleX, float scaleY) {}

    void checkInitialSelfCollision();

    virtual sf::Color getPixelColor() {return sf::Color::White; }
    virtual bool isRenderSprite() { return false; }

    static void prepare() {}

    virtual std::string getName() = 0;
};

vector<Projectile*> projectiles; // TODO make not global



struct Vec2f {
    float x = 0, y = 0;
};

struct Smoke;

struct Map {

    int w = 16, h = 16;
    int scaleX = 3, scaleY = 3;

    float viewportX = 0, viewportY = 0;


    sf::RectangleShape rect, rectHover, bgRect;
    sf::RectangleShape rectGround, rectDirt, rectWater, rectOil;

    bool useDebugColors = false;

    vector<Character*> characters;

    vector<Vec2f> tileToWorldCoordinates;

    sf::SoundBuffer fireSoundBuffer;
    SoundInstance *fireSoundInstance = nullptr;

    void addCharacters(vector<Character*> &characters) {
        this->characters = characters;
    }


    struct Tile {
        enum Type { None, Ground, Dirt, Water, Oil };

        Type type = None;
        bool firmlyGrounded = false;

        sf::Color color;

        bool wasFlying = false;

        bool flammable = false;
        bool burning = false;
        float igniting = 0;

        float health = 1.0;

        float flameDealDamagePerSecond = 0;

        void reset() {
            type = None;
            flammable = false;
            firmlyGrounded = false;
        }
    };

    struct TilePainter {
        enum PainterType { Brush, Line, Rect };

        Tile::Type type = Tile::None;

        bool flammable = false;

        PainterType painterType = Brush;
        int brushSize = 1;



        //enum ColorMode { TileType, Random };
        //ColorMode colorMode = ColorMode::Random;

        void setTileType(int type) {
            this->type = Tile::Type(type);
        }

        void setPainterType(int painterType) {
            this->painterType = PainterType(painterType);
        }
    };

    TilePainter tilePainter;


    const Tile emptyTile;

    vector<Tile> tiles = vector<Tile>();

    sf::Text text;

    int hoverX = 0, hoverY = 0;

    enum GameStatus { Active, Victory, GameOver };
    GameStatus gameStatus = GameStatus::Active;



    int debugTileRenderMode = 1;

    sf::Image tileImage;
    sf::Texture tileTexture;
    sf::Sprite tileSprite;
    sf::Uint8* tilePixels = nullptr;

    sf::Uint8 tileAlpha = 255;
    sf::Uint8 noneR = 50, noneG = 50, noneB = 50;
    sf::Uint8 groundR = 150, groundG = 150, groundB = 150;
    sf::Uint8 dirtR = 204, dirtG = 170, dirtB = 116;
    sf::Uint8 waterR = 100, waterG = 100, waterB = 255;
    static const sf::Uint8 oilR = 90, oilG = 80, oilB = 50;

    sf::Color getNoneTileColor() {
        //return sf::Color(noneR, noneG, noneB, tileAlpha);
        int c = randi(40, 60);
        return sf::Color(c, c, c, 250);
    }

    sf::Color getGroundTileColor(int ind = -1) {
        if(ind >= 0) {
            return sf::Color(fgPixels[ind * 4 + 0],
                             fgPixels[ind * 4 + 1], 
                             fgPixels[ind * 4 + 2], 
                             fgPixels[ind * 4 + 3]);
        }
        else {
            int c = randi(30, 220);
            return sf::Color(c, c, c, tileAlpha);
        }
    }

    sf::Color getFlammableGroundTileColor(int ind = -1) {
        if(ind >= 0) {
            return sf::Color(fgPixels2[ind * 4 + 0],
                             fgPixels2[ind * 4 + 1], 
                             fgPixels2[ind * 4 + 2], 
                             fgPixels2[ind * 4 + 3]);
        }
        else {
            return sf::Color(randi(130, 150), randi(90, 110), randi(50, 70), tileAlpha);
        }
    }

    sf::Color getDirtColor() {
        return sf::Color(randi(185, 220), randi(155, 185), randi(110, 130), tileAlpha);
    }

    sf::Color getFlammableDirtColor() {
        return sf::Color(randi(130, 150), randi(90, 110), randi(50, 70), tileAlpha);
    }

    sf::Color getWaterTileColor() {
        return sf::Color(randi(90, 110), randi(90, 110), randi(220, 255), 150);
    }

    sf::Color getOilTileColor() {
        return sf::Color(randi(80, 100), randi(70, 90), randi(40, 60), 150);
    }

    sf::Uint8* bgPixels = nullptr;
    sf::Image bgImage;
    sf::Texture bgTexture;
    sf::Sprite bgSprite;

    sf::Uint8 *landGrowerPixels = nullptr;

    sf::Image bgImage2;
    sf::Texture bgTexture2;
    sf::Sprite bgSprite2;
    sf::Uint8* bgPixels2 = nullptr;
    int bgPixels2W = 0, bgPixels2H = 0;

    sf::Image fgImage;
    sf::Texture fgTexture;
    sf::Sprite fgSprite;
    sf::Uint8* fgPixels = nullptr;

    sf::Image fgImage2;
    sf::Texture fgTexture2;
    sf::Sprite fgSprite2;
    sf::Uint8* fgPixels2 = nullptr;


    Noise noise;

    vector<std::thread> mapRenderThreads;
    vector<std::thread> mapUpdateThreads;
    std::thread noisePixelsThread;

    /*
    sf::RenderTexture occlusionRenderTexture[2];
    sf::Sprite occlusionSpriteDebug;
    int occlusionRenderTextureW = 2048;
    int occlusionRenderTextureH = 1;

    sf::Shader occlusionShader, occlusionShaderPost;
    */

    /*sf::Shader bgShader;
    //sf::Sprite bgSprite;
    sf::RectangleShape bgShaderRect;

    sf::Texture occlusionTexturePost;
    sf::Sprite occlusionSpritePost;*/

    //enum FieldOfVision { NoLimits, Halo, Occlusion };
    //FieldOfVision fieldOfVision = NoLimits;


    struct LargeTile {
        int w = 0, h = 0;
        sf::Image image;
        sf::Texture texture;
        sf::Sprite sprite;
        const sf::Uint8* pixels = nullptr;

        bool loadFromFile(const std::string &filename) {
            std::string path = "data/textures/tiles/"+filename;
            if(!image.loadFromFile(path)) {
                printf("Couldn't open file '%s'!\n", path.c_str());
                return false;
            }
            texture.loadFromImage(image);
            sprite.setTexture(texture, true);
            sf::Vector2u size = image.getSize();
            w = size.x;
            h = size.y;

            pixels = image.getPixelsPtr();

            return true;
        }
    };

    struct LargeTileSet {

        std::vector<LargeTile*> largeTiles;

        ~LargeTileSet() {
            for(int i=0; i<largeTiles.size(); i++) {
                delete largeTiles[i];
            }
        }

        bool loadTileSetFromFolder(const std::string &tileSetName, int numTiles) {
            std::string path = tileSetName + "/";
            for(int i=0; i<numTiles; i++) {
                int index = i + 1;
                std::string fullPath = path + "tile";
                if(numTiles < 100) {
                    if(index < 10) {
                        fullPath += "0"+ std::to_string(index) + ".png";
                    }
                    else {
                        fullPath += std::to_string(index) + ".png";
                    }
                }
                else {
                    printf("ERROR at LargeTileSet.loadTileSetFromFolder(). Too many tiles! (Please fix the code.)\n");
                    return false;
                }
                LargeTile *largeTile = new LargeTile();
                
                if(!largeTile->loadFromFile(fullPath)) {
                    printf("ERROR at LargeTileSet.loadTileSetFromFolder(). Couldn't open a tile!\n");
                    return false;
                }
                largeTiles.push_back(largeTile);
            }
            return true;
        }
    };

    LargeTileSet largeTileSet01;    // rocky
    LargeTileSet largeTileSet02;    // woody


    void prepareNoisePixels() {
        if(noisePixelsThread.joinable()) {
            noisePixelsThread.join();
        }

        noisePixelsThread = thread(prepareNoisePixelsThread, this, randf(0, 10000), randf(0, 10000));
    }

    static void prepareNoisePixelsThread(Map *map, float dx, float dy) {
        for(int x=0; x<map->w; x++) {
            for(int y=0; y<map->h; y++) {
                int i = (x + y*map->w) * 4;
                int c = map->noise.getFBMi(dx + (float)x/map->w*4.0, dy + (float)y/map->w*4.0, 3, 0.5, 3.0, 0, 255);
                map->landGrowerPixels[i+0] = c;
                map->landGrowerPixels[i+1] = c;
                map->landGrowerPixels[i+2] = c;
                map->landGrowerPixels[i+3] = 255;
            }
        }
    }

    ~Map() {
        if(noisePixelsThread.joinable()) {
            noisePixelsThread.join();
        }

        for(int i=0; i<mapRenderThreads.size(); i++) {
            if(mapRenderThreads[i].joinable()) {
                mapRenderThreads[i].join();
            }
        }
        mapRenderThreads.clear();

        for(int i=0; i<mapUpdateThreads.size(); i++) {
            if(mapUpdateThreads[i].joinable()) {
                mapUpdateThreads[i].join();
            }
        }
        mapUpdateThreads.clear();

        tiles.clear();
    }

    float mw_ = 1;
    float mh_ = 1;
    float mx_ = 1;
    float my_ = 1;


    int levelize(int c, int numLevels) {
        return (c / (255/numLevels) * (255/numLevels));
    }


    Map(sf::Font &font, int w = 16, int h = 16, int scaleX = 3, int scaleY = 3) {
        this->w = w;
        this->h = h;
        this->scaleX = scaleX;
        this->scaleY = scaleY;

        mw_ = w * scaleX;
        mh_ = h * scaleY;
        mx_ = screenW/2 - mw_/2;
        my_ = screenH/2 - mh_/2;


        if(!fireSoundBuffer.loadFromFile("data/audio/fire.ogg")) {
            printf("Couldn't open file 'data/audio/fire.ogg'!\n");
            return;
        }
        fireSoundInstance = soundWrapper.playSoundBuffer(fireSoundBuffer, true, 0.0);


        gravity *= scaleY;



        largeTileSet01.loadTileSetFromFolder("set01", 7);
        largeTileSet02.loadTileSetFromFolder("set02", 5);



        tileToWorldCoordinates.resize(w*h);
        for(int x=0; x<w; x++) {
            for(int y=0; y<h; y++) {
                tileToWorldCoordinates[x + y*w].x = x * scaleX;
                tileToWorldCoordinates[x + y*w].y = y * scaleY;
            }
        }

        tilePixels = new sf::Uint8[w*h*4];
        for(int x=0; x<w; x++) {
            for(int y=0; y<h; y++) {
                int i = (x + y*w) * 4;
                tilePixels[i+0] = randi(0, 255);
                tilePixels[i+1] = randi(0, 255);
                tilePixels[i+2] = randi(0, 255);
                tilePixels[i+3] = 255;
            }
        }

        tileImage.create(w, h, tilePixels);
        tileTexture.loadFromImage(tileImage);
        tileSprite.setTexture(tileTexture, true);




        //bgImage2.loadFromFile("data/textures/darkened/bampw-stone-surface.jpg");
        //bgImage2.loadFromFile("data/textures/darkened/cracked-grunge-wall-texture-2.jpg");
        bgImage2.loadFromFile("data/textures/darkened/grayscale-stone-surface.jpg");
        //bgImage2.loadFromFile("data/textures/darkened/grunge-scratched-wall.jpg");
        //bgImage2.loadFromFile("data/textures/gradient.png");
        
        
        bgTexture2.loadFromImage(bgImage2);
        bgTexture2.setRepeated(true);
        bgSprite2.setTexture(bgTexture2, true);
        const sf::Uint8 *bgPixelsTemp2 = bgImage2.getPixelsPtr();
        sf::Vector2u bgImage2Size = bgImage2.getSize();

        /*bgPixels2W = w + 300;
        bgPixels2H = h + 300;
        bgPixels2 = new sf::Uint8[bgPixels2W*bgPixels2H*4];

        for(int x=0; x<bgPixels2W; x++) {
            for(int y=0; y<bgPixels2H; y++) {
                int i = (x + y*bgPixels2W) * 4;
                int j = (x + y*bgImage2Size.x) * 4;
                bgPixels2[i+0] = levelize(bgPixelsTemp2[j+0], 20);
                bgPixels2[i+1] = levelize(bgPixelsTemp2[j+0], 20);
                bgPixels2[i+2] = levelize(bgPixelsTemp2[j+0], 20);
                bgPixels2[i+3] = 255;//levelize(bgPixelsTemp2[j+0], 20);
            }
        }*/
        bgPixels2 = new sf::Uint8[w*h*4];

        if(w > bgImage2Size.x || h > bgImage2Size.y) {
            for(int x=0; x<w; x++) {
                for(int y=0; y<h; y++) {
                    int i = (x + y*w) * 4;
                    sf::Color color = getNoneTileColor();
                    bgPixels2[i+0] = color.r;
                    bgPixels2[i+1] = color.g;
                    bgPixels2[i+2] = color.b;
                    bgPixels2[i+3] = 255;//levelize(bgPixelsTemp2[j+0], 20);
                }
            }

        }
        else {
            for(int x=0; x<w; x++) {
                for(int y=0; y<h; y++) {
                    int i = (x + y*w) * 4;
                    int j = (x + y*bgImage2Size.x) * 4;
                    bgPixels2[i+0] = levelize(bgPixelsTemp2[j+0], 20);
                    bgPixels2[i+1] = levelize(bgPixelsTemp2[j+0], 20);
                    bgPixels2[i+2] = levelize(bgPixelsTemp2[j+0], 20);
                    bgPixels2[i+3] = 255;//levelize(bgPixelsTemp2[j+0], 20);
                }
            }
        }









        bgPixels = new sf::Uint8[w*h*4];

        for(int x=0; x<w; x++) {
            for(int y=0; y<h; y++) {
                int i = (x + y*w) * 4;
                sf::Color c = getNoneTileColor();
                bgPixels[i+0] = c.r;
                bgPixels[i+1] = c.g;
                bgPixels[i+2] = c.b;
                bgPixels[i+3] = c.a;
            }
        }

        bgImage.create(w, h, bgPixels);
        bgTexture.loadFromImage(bgImage);
        bgSprite.setTexture(bgTexture, true);





        fgImage.loadFromFile("data/textures/darkened/bampw-stone-surface(1).jpg");
        fgTexture.loadFromImage(fgImage);
        fgTexture.setRepeated(true);
        fgSprite.setTexture(fgTexture, true);
        const sf::Uint8 *fgPixelsTemp = fgImage.getPixelsPtr();
        sf::Vector2u fgImageSize = fgImage.getSize();

        fgPixels = new sf::Uint8[w*h*4];
        
        //printf("w %d, h %d, fg.w %d, fg.h %d\n", w, h, fgImageSize.x, fgImageSize.y);

        if(w > fgImageSize.x || h > fgImageSize.y) {
            for(int x=0; x<w; x++) {
                for(int y=0; y<h; y++) {
                    int i = (x + y*w) * 4;
                    sf::Color color = getGroundTileColor();
                    fgPixels[i+0] = color.r;
                    fgPixels[i+1] = color.g;
                    fgPixels[i+2] = color.b;
                    fgPixels[i+3] = 255;//levelize(bgPixelsTemp2[j+0], 20);
                }
            }
        }
        else {
            for(int x=0; x<w; x++) {
                for(int y=0; y<h; y++) {
                    int i = (x + y*w) * 4;
                    int j = (x + y*fgImageSize.x) * 4;
                    fgPixels[i+0] = levelize(fgPixelsTemp[j+0], 6);
                    fgPixels[i+1] = levelize(fgPixelsTemp[j+0], 6);
                    fgPixels[i+2] = levelize(fgPixelsTemp[j+0], 6);
                    fgPixels[i+3] = 255;//levelize(bgPixelsTemp2[j+0], 20);
                }
            }
        }


        fgImage2.loadFromFile("data/textures/darkened/weathered-wooden-surface.jpg");
        //<a href="https://www.freepik.com/free-photo/weathered-wooden-surface_968894.htm#query=woodtexture&position=12&from_view=keyword&track=ais?sign-up=google">Image by fwstudio</a> on Freepik
        fgTexture2.loadFromImage(fgImage2);
        fgTexture2.setRepeated(true);
        fgSprite2.setTexture(fgTexture2, true);
        const sf::Uint8 *fgPixelsTemp2 = fgImage2.getPixelsPtr();
        sf::Vector2u fgImageSize2 = fgImage2.getSize();

        fgPixels2 = new sf::Uint8[w*h*4];
        
        if(w > fgImageSize2.x || h > fgImageSize2.y) {
            for(int x=0; x<w; x++) {
                for(int y=0; y<h; y++) {
                    int i = (x + y*w) * 4;
                    sf::Color color = getFlammableGroundTileColor();
                    fgPixels2[i+0] = color.r;
                    fgPixels2[i+1] = color.g;
                    fgPixels2[i+2] = color.b;
                    fgPixels2[i+3] = 255;//levelize(bgPixelsTemp2[j+0], 20);
                }
            }
        }
        else {
            for(int x=0; x<w; x++) {
                for(int y=0; y<h; y++) {
                    int i = (x + y*w) * 4;
                    int j = (x + y*fgImageSize2.x) * 4;

                    fgPixels2[i+0] = levelize(fgPixelsTemp2[j+0], 10);
                    fgPixels2[i+1] = levelize(fgPixelsTemp2[j+1], 10);
                    fgPixels2[i+2] = levelize(fgPixelsTemp2[j+2], 10);
                    fgPixels2[i+3] = 255;//levelize(bgPixelsTemp2[j+0], 20);
                }
            }
        }



        float dx = randf(0, 10000), dy = randf(0, 10000);
        landGrowerPixels = new sf::Uint8[w*h*4];
        for(int x=0; x<w; x++) {
            for(int y=0; y<h; y++) {
                int i = (x + y*w) * 4;
                int c = noise.getFBMi(dx + (float)x/w*4.0, dy + (float)y/w*4.0, 3, 0.5, 3.0, 0, 255);
                landGrowerPixels[i+0] = c;
                landGrowerPixels[i+1] = c;
                landGrowerPixels[i+2] = c;
                landGrowerPixels[i+3] = 255;
            }
        }




        //tiles = vector<Tile>(w*h);
        tiles.resize(w*h);


        restart();

        rect = sf::RectangleShape(sf::Vector2f(scaleX, scaleY));
        rect.setFillColor(sf::Color(250, 250, 250, 255));

        bgRect = sf::RectangleShape(sf::Vector2f(w*scaleX, h*scaleY));
        bgRect.setFillColor(sf::Color(50, 50, 50, 255));

        rectGround = sf::RectangleShape(sf::Vector2f(scaleX, scaleY));
        rectGround.setFillColor(sf::Color(150, 150, 150, 255));

        rectDirt = sf::RectangleShape(sf::Vector2f(scaleX, scaleY));
        rectDirt.setFillColor(sf::Color(204, 170, 116, 255));

        rectWater = sf::RectangleShape(sf::Vector2f(scaleX, scaleY));
        rectWater.setFillColor(sf::Color(100, 100, 255, 255));

        rectOil = sf::RectangleShape(sf::Vector2f(scaleX, scaleY));
        rectOil.setFillColor(sf::Color(oilR, oilG, oilB, 255));


        text.setFont(font);
        text.setString("testing...");
        text.setCharacterSize((int)((float)scaleY*0.75));
        text.setFillColor(sf::Color::Black);


        //occlusionRenderTextureW = screenW;
        /*sf::ContextSettings settings;
        settings.depthBits = 32;
        for(int i=0; i<2; i++) {
            occlusionRenderTexture[i].create(occlusionRenderTextureW, occlusionRenderTextureH, settings);
        }

        occlusionSpriteDebug.setTexture(occlusionRenderTexture[0].getTexture(), true);

        if(!occlusionShader.loadFromFile("data/shaders/occlusion2.frag", sf::Shader::Fragment)) {
            printf("Couldn't load shader 'data/shaders/occlusion2.frag'!\n");
        }*/

        /*if(!occlusionShader.loadFromFile("data/shaders/occlusion.vert", "data/shaders/occlusion.frag")) {
            printf("Couldn't open shaders 'data/shaders/occlusion.vert' and/or 'data/shaders/occlusion.frag'");
        }*/

        /*if(!occlusionShaderPost.loadFromFile("data/shaders/occlusionPost2.frag", sf::Shader::Fragment)) {
            printf("Couldn't load shader 'data/shaders/occlusionPost2.frag'!\n");
        }
        occlusionTexturePost.create(w, h);
        occlusionSpritePost.setTexture(occlusionTexturePost, true);*/


        //bgShaderRect = sf::RectangleShape(sf::Vector2f(w, h));
        /*bgShaderRect.setSize(sf::Vector2f(w, h));
        //bgShaderRect.setOrigin(w/2, h/2);
        if(!bgShader.loadFromFile("data/shaders/bg.frag", sf::Shader::Fragment)) {
            printf("Couldn't load shader 'data/shaders/bg.frag'!\n");
        }*/


        initEarthquakes();
    }

    float asdTimer = 0;
    int asdCounter = 0;

    void asd(TilePainter &tilePainter, int threshold = 111);


    void asd2(TilePainter &tilePainter, int threshold = 111);


    void restart() {
        gameStatus = GameStatus::Active;


        for(int i=0; i<w*h; i++) {
            tiles[i].reset();
        }

    }






    struct Earthquake {
        float duration = 7.7;
        float duration2 = 15;
        float timer = 0;
        int mode = 0;
        float extend = 9;

        void init(Map &map) {
            timer = 0;

            if(mode == 0) {
                for(int i=0; i<map.w*map.h; i++) {
                    if(map.tiles[i].type == Map::Tile::Type::Ground) {
                        map.tiles[i].type = Map::Tile::Type::Dirt;
                    }
                }
            }
        }

        bool update(float dt, float &viewportX, float &viewportY, Map &map) {
            timer += dt;

            if(timer >= duration2) {
                if(mode == 0) {
                    for(int i=0; i<map.w*map.h; i++) {
                        if(map.tiles[i].type == Map::Tile::Type::Dirt) {
                            map.tiles[i].type = Map::Tile::Type::Ground;
                        }
                    }
                }
                timer = 0;
                return false;
            }
            else if(timer >= duration) {
                return true;
            }

            if(mode == 0) {
                viewportX += randf2(-extend, extend);
                viewportY += randf2(-extend, extend);                
            }
            if(mode == 1) {
                float e = extend - mapf(timer, 0, duration, 0, extend);
                viewportX += randf2(-e, e);
                viewportY += randf2(-e, e);                
            }

            return true;
        }
    };

    std::vector<Earthquake> activeEarthquakes;

    enum class EarthquakeType { earthquakeActual, earthquakeNuclearBomb, earthquakeDoomsDay };

    // TODO These should probably be defined in the client classes that invoke these
    Earthquake earthquakeActual;
    Earthquake earthquakeNuclearBomb;
    Earthquake earthquakeDoomsDay;

    void initEarthquakes() {

        earthquakeNuclearBomb.duration = 3.0;
        earthquakeNuclearBomb.duration2 = 3.0;
        earthquakeNuclearBomb.extend = 20.0;
        earthquakeNuclearBomb.mode = 1;

        earthquakeDoomsDay.duration = 1.5;
        earthquakeDoomsDay.duration2 = 1.5;
        earthquakeDoomsDay.extend = 12.0;
        earthquakeDoomsDay.mode = 1;
    }

    void startEarthquake(EarthquakeType type) {
        if(type == EarthquakeType::earthquakeActual) {
            activeEarthquakes.push_back(earthquakeActual);
        }
        else if(type == EarthquakeType::earthquakeNuclearBomb) {
            activeEarthquakes.push_back(earthquakeNuclearBomb);
        }
        else if(type == EarthquakeType::earthquakeDoomsDay) {
            activeEarthquakes.push_back(earthquakeDoomsDay);
        }

        activeEarthquakes[activeEarthquakes.size()-1].init(*this);
    }

    void updateEarthquakes(float dt) {    
        viewportX = 0;
        viewportY = 0;

        for(int i=activeEarthquakes.size()-1; i>=0; i--) {
            bool r = activeEarthquakes[i].update(dt, viewportX, viewportY, *this);
            if(!r) {
                activeEarthquakes.erase(activeEarthquakes.begin() + i);
            }
        }
    }




    float noisePixelsThreadTimer = 0;

    int numBurningTiles = 0;
    int numOccupiedTiles = 0;

    void update(float dt) {

        noisePixelsThreadTimer += dt;
        if(noisePixelsThreadTimer >= 180) {
            prepareNoisePixels();
            noisePixelsThreadTimer = 0;
        }

        updateEarthquakes(dt);


        numBurningTiles = 0;
        numOccupiedTiles = 0;

        //update_t(this, dt, 0, 1);

        for(int i=0; i<numCores; i++) {
            mapUpdateThreads.push_back(std::thread(update_t, this, dt, i, numCores));
        }

        for(int i=0; i<numCores; i++) {
            if(mapUpdateThreads.size() > i && mapUpdateThreads[i].joinable()) {
                mapUpdateThreads[i].join();
            }
        }
        mapUpdateThreads.clear();

        fireSoundInstance->setVolume(min(0.1*numBurningTiles, 100));
    }

    static void update_t(Map *map, float dt, int threadNum, int numThreads) {


        for(int ky = map->h-1; ky>=0; ky-=1) {

            //bool updateLeftToRight = randf() > 0.5;
            bool updateLeftToRight = true;  // TODO fix me
            int y = ky;


            for(int kx = threadNum; kx<map->w; kx+=numThreads) {
                int x = updateLeftToRight ? map->w-1 - kx : kx;

                int i = x + y*map->w;

                if(map->tiles[i].type != Map::Tile::None) {
                    map->numOccupiedTiles++;
                }

                if(map->tiles[i].burning && map->tiles[i].type != Map::Tile::None) {
                    map->tiles[i].health -= dt;
                    map->numBurningTiles++;

                    //float t = mapf(timer.fps, 50, 60, 0, 0.1); // TODO fix


                    if(((map->tiles[i].type != Map::Tile::Oil && randf2(0, 1) < 0.05)) || (map->tiles[i].type == Map::Tile::Oil && y > 0 && map->tiles[i-map->w].type == Map::Tile::None && randf2(0, 1) < 0.05)) {
                        if(map->tiles[i].flameDealDamagePerSecond > 0) {
                            createSmoke(map->scaleX*x, map->scaleY*(y-1), map->characters, 10, 20, 200);
                            createFlame(map->scaleX*x, map->scaleY*(y-1), map->characters, 10, 10, 100, map->tiles[i].flameDealDamagePerSecond);
                        }
                        else {
                            createSmoke(map->scaleX*x, map->scaleY*(y-1), map->characters, 1, 20, 200);
                            createFlame(map->scaleX*x, map->scaleY*(y-1), map->characters, 1, 10, 100, 0);
                        }
                    }

                    /*if(((map->tiles[i].type != Map::Tile::Oil && randf2(0, 1) < 0.1)) || (map->tiles[i].type == Map::Tile::Oil && y > 0 && map->tiles[i-map->w].type == Map::Tile::None && randf2(0, 1) < 0.1)) {
                        createSmoke(map->scaleX*x, map->scaleY*(y-1), map->characters, 1, 20, 200);
                        createFlame(map->scaleX*x, map->scaleY*(y-1), map->characters, 1, 10, 100);
                    }*/

                    bool igniteAdjacentTile = false;

                    if(map->tiles[i].health < 0) {
                        map->tiles[i].health = 0;
                        map->tiles[i] = map->emptyTile;
                        //igniteAdjacentTile = true;
                        igniteAdjacentTile = randf2(0, 1) < 0.75;
                    }

                    if(map->tiles[i].type == Map::Tile::Oil) {
                        igniteAdjacentTile = true;
                    }

                    if(igniteAdjacentTile && map->tiles[i].type == Map::Tile::Oil) {
                        if(map->isTileWithin(x+1, y) && map->tiles[i+1].flammable && map->isTileWithin(x+1, y-1) && map->tiles[i+1-map->w].type == Tile::None && !map->tiles[i+1].burning) {
                            map->tiles[i+1].burning = true;
                            if(map->tiles[i+1].health <= 0) map->tiles[i+1].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x-1, y) && map->tiles[i-1].flammable && map->isTileWithin(x-1, y-1) && map->tiles[i-1-map->w].type == Map::Tile::None && !map->tiles[i-1].burning) {
                            map->tiles[i-1].burning = true;
                            if(map->tiles[i-1].health <= 0) map->tiles[i-1].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x, y+1) && map->tiles[i+map->w].flammable && map->isTileWithin(x, y-1) && map->tiles[i-map->w].type == Map::Tile::None && !map->tiles[i+map->w].burning) {
                            map->tiles[i+map->w].burning = true;
                            if(map->tiles[i+map->w].health <= 0) map->tiles[i+map->w].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x, y-1) && map->tiles[i-map->w].flammable && map->isTileWithin(x, y-2) && map->tiles[i-map->w*2].type == Map::Tile::None && !map->tiles[i-map->w].burning) {
                            map->tiles[i-map->w].burning = true;
                            if(map->tiles[i-map->w].health <= 0) map->tiles[i-map->w].health = randf2(1, 10);
                        }

                        if(map->isTileWithin(x+1, y+1) && map->tiles[i+1+map->w].flammable && map->isTileWithin(x+1, y-1) && map->tiles[i+1-map->w].type == Map::Tile::None && !map->tiles[i+1+map->w].burning) {
                            map->tiles[i+1+map->w].burning = true;
                            if(map->tiles[i+1+map->w].health <= 0) map->tiles[i+1+map->w].health = randf2(1, 10);
                        }//
                        if(map->isTileWithin(x+1, y-1) && map->tiles[i+1-map->w].flammable && map->isTileWithin(x+1, y-2*map->w) && map->tiles[i].type == Map::Tile::None && !map->tiles[i+1-map->w].burning) {
                            map->tiles[i+1-map->w].burning = true;
                            if(map->tiles[i+1-map->w].health <= 0) map->tiles[i+1-map->w].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x-1, y-1) && map->tiles[i-1-map->w].flammable && map->isTileWithin(x-1, y-2) && map->tiles[i-1-2*map->w].type == Map::Tile::None && !map->tiles[i-1-map->w].burning) {
                            map->tiles[i-1-map->w].burning = true;
                            if(map->tiles[i-1-map->w].health <= 0) map->tiles[i-1-map->w].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x-1, y+1) && map->tiles[i-1+map->w].flammable && map->isTileWithin(x+1, y-2) && map->tiles[i-1-2*map->w].type == Map::Tile::None && !map->tiles[i-map->w+map->w].burning) {
                            map->tiles[i-1+map->w].burning = true;
                            if(map->tiles[i-1+map->w].health <= 0) map->tiles[i-1+map->w].health = randf2(1, 10);
                        }//
                    }

                    else if(igniteAdjacentTile) {
                        if(map->isTileWithin(x+1, y) && map->tiles[i+1].flammable) {
                            map->tiles[i+1].burning = true;
                            if(map->tiles[i+1].health <= 0) map->tiles[i+1].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x-1, y) && map->tiles[i-1].flammable) {
                            map->tiles[i-1].burning = true;
                            if(map->tiles[i-1].health <= 0) map->tiles[i-1].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x, y+1) && map->tiles[i+map->w].flammable) {
                            map->tiles[i+map->w].burning = true;
                            if(map->tiles[i+map->w].health <= 0) map->tiles[i+map->w].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x, y-1) && map->tiles[i-map->w].flammable) {
                            map->tiles[i-map->w].burning = true;
                            if(map->tiles[i-map->w].health <= 0) map->tiles[i-map->w].health = randf2(1, 10);
                        }

                        if(map->isTileWithin(x+1, y+1) && map->tiles[i+1+map->w].flammable) {
                            map->tiles[i+1+map->w].burning = true;
                            if(map->tiles[i+1+map->w].health <= 0) map->tiles[i+1+map->w].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x+1, y-1) && map->tiles[i+1-map->w].flammable) {
                            map->tiles[i+1-map->w].burning = true;
                            if(map->tiles[i+1-map->w].health <= 0) map->tiles[i+1-map->w].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x-1, y-1) && map->tiles[i-1-map->w].flammable) {
                            map->tiles[i-1-map->w].burning = true;
                            if(map->tiles[i-1-map->w].health <= 0) map->tiles[i-1-map->w].health = randf2(1, 10);
                        }
                        if(map->isTileWithin(x-1, y+1) && map->tiles[i-1+map->w].flammable) {
                            map->tiles[i-1+map->w].burning = true;
                            if(map->tiles[i-1+map->w].health <= 0) map->tiles[i-1+map->w].health = randf2(1, 10);
                        }
                    }

                    if(map->isTileWithin(x+1, y) && map->tiles[i+1].type != Map::Tile::None)  {
                        if(map->isTileWithin(x-1, y) && map->tiles[i-1].type != Map::Tile::None)  {
                            if(map->isTileWithin(x, y+1) && map->tiles[i+map->w].type != Map::Tile::None)  {
                                if(map->isTileWithin(x, y-1) && map->tiles[i-map->w].type != Map::Tile::None) {
                                    map->tiles[i].burning = false;
                                }
                            }
                        }
                    }

                }


                if(map->tiles[i].type == Map::Tile::Dirt && !map->tiles[i].burning) {
                    if(y < map->h-1 && (map->tiles[i+map->w].type == Map::Tile::None || map->tiles[i+map->w].type == Map::Tile::Water || map->tiles[i+map->w].type == Map::Tile::Oil)) {
                        Tile tmp = map->tiles[i+map->w];
                        map->tiles[i+map->w] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                    else if(y < map->h-1 && x > 0 && (map->tiles[i+map->w-1].type == Map::Tile::None || map->tiles[i+map->w-1].type == Map::Tile::Water || map->tiles[i+map->w-1].type == Map::Tile::Oil)) {
                        Tile tmp = map->tiles[i+map->w-1];
                        map->tiles[i+map->w-1] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                    else if(y < map->h-1 && x < map->w-1 && (map->tiles[i+map->w+1].type == Map::Tile::None || map->tiles[i+map->w+1].type == Map::Tile::Water || map->tiles[i+map->w+1].type == Map::Tile::Oil)) {
                        Tile tmp = map->tiles[i+map->w+1];
                        map->tiles[i+map->w+1] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }

                }
                else if(map->tiles[i].type == Map::Tile::Dirt && map->tiles[i].burning) {
                    if(y < map->h-1 && (map->tiles[i+map->w].type == Map::Tile::None || map->tiles[i+map->w].type == Map::Tile::Water || map->tiles[i+map->w].type == Map::Tile::Oil)) {
                        Tile tmp = map->tiles[i+map->w];
                        map->tiles[i+map->w] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                }

                /*if(map->tiles[i].type == Map::Tile::Dirt) {
                    if(y < map->h-1 && (map->tiles[i+map->w].type == Map::Tile::None || map->tiles[i+map->w].type == Map::Tile::Water || map->tiles[i+map->w].type == Map::Tile::Oil)) {
                        Tile tmp = map->tiles[i+map->w];
                        map->tiles[i+map->w] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                    else if(y < map->h-1 && x > 0 && (map->tiles[i+map->w-1].type == Map::Tile::None || map->tiles[i+map->w-1].type == Map::Tile::Water || map->tiles[i+map->w-1].type == Map::Tile::Oil)) {
                        Tile tmp = map->tiles[i+map->w-1];
                        map->tiles[i+map->w-1] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                    else if(y < map->h-1 && x < map->w-1 && (map->tiles[i+map->w+1].type == Map::Tile::None || map->tiles[i+map->w+1].type == Map::Tile::Water || map->tiles[i+map->w+1].type == Map::Tile::Oil)) {
                        Tile tmp = map->tiles[i+map->w+1];
                        map->tiles[i+map->w+1] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }

                }*/
                if(map->tiles[i].type == Map::Tile::Water) {
                    if(y < map->h-1 && (map->tiles[i+map->w].type == Map::Tile::None || map->tiles[i+map->w].type == Map::Tile::Oil)) {
                        Tile tmp = map->tiles[i+map->w];
                        map->tiles[i+map->w] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                    else if(x > 0 && (map->tiles[i-1].type == Map::Tile::None || map->tiles[i-1].type == Map::Tile::Oil)) {
                        Tile tmp = map->tiles[i-1];
                        map->tiles[i-1] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                    else if(x < map->w-1 && (map->tiles[i+1].type == Map::Tile::None || map->tiles[i+1].type == Map::Tile::Oil)) {
                        Tile tmp = map->tiles[i+1];
                        map->tiles[i+1] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                }

                if(map->tiles[i].type == Map::Tile::Oil) {
                    if(y < map->h-1 && map->tiles[i+map->w].type == Map::Tile::None) {
                        Tile tmp = map->tiles[i+map->w];
                        map->tiles[i+map->w] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                    else if(x > 0 && map->tiles[i-1].type == Map::Tile::None) {
                        Tile tmp = map->tiles[i-1];
                        map->tiles[i-1] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                    else if(x < map->w-1 && map->tiles[i+1].type == Map::Tile::None) {
                        Tile tmp = map->tiles[i+1];
                        map->tiles[i+1] = map->tiles[i];
                        map->tiles[i] = tmp;
                    }
                }

            }
        }

        //map->fireSoundInstance->setVolume(min(0.1*map->numBurningTiles, 100));
    }

    void renderBg(float screenW, float screenH, sf::RenderWindow &window) {
        /*
        float mw = w * scaleX;
        float mh = h * scaleY;
        float mx = screenW/2 - mw/2;
        float my = screenH/2 - mh/2;

        if(useDebugColors) {
            bgRect.setPosition(mx, my);
            window.draw(bgRect);
        }
        else {
            bgSprite.setPosition(mx, my);
            bgSprite.setScale(scaleX, scaleY);
            window.draw(bgSprite);
        }*/
    }

    void render(float screenW, float screenH, sf::RenderWindow &window);

    static void render_t(Map *map, int threadNum, int numThreads, float screenW, float screenH, float mw, float mh, float mx, float my, float ax[], float ay[], float bx[], float by[]);

    void renderPixelProjectiles(float screenW, float screenH) {
        if(areProjectilesPixelated) {
            for(int i=0; i<projectiles.size(); i++) {
                if(projectiles[i]->isRenderSprite()) continue;

                int px = mapX(projectiles[i]->x, screenW);
                int py = mapY(projectiles[i]->y, screenH);

                if(px != -1 && py != -1) {
                    sf::Color c = projectiles[i]->getPixelColor();
                    int ind = (px + py*w) * 4;
                    float t = c.a / 255.0;
                    tilePixels[ind + 0] = t * c.r + (1.0 - t) * tilePixels[ind + 0];
                    tilePixels[ind + 1] = t * c.g + (1.0 - t) * tilePixels[ind + 1];
                    tilePixels[ind + 2] = t * c.b + (1.0 - t) * tilePixels[ind + 2];
                    tilePixels[ind + 3] = 250; // c.a * c.a + (1.0 - c.a) * tilePixels[ind + 3];
                    //printf("Pixelating (%d, %d)\n", px, py);
                }
            }
        }
    }



    void renderFinal(float screenW, float screenH, sf::RenderWindow &window);/* {
        float mw = w * scaleX;
        float mh = h * scaleY;
        float mx = screenW/2 - mw/2;
        float my = screenH/2 - mh/2;

        //renderBg(screenW, screenH, window);

        tileImage.create(w, h, tilePixels);
        tileTexture.loadFromImage(tileImage);
        //tileSprite.setTexture(tileTexture, true);

        tileSprite.setPosition(mx, my);
        tileSprite.setScale(scaleX, scaleY);
        window.draw(tileSprite);

        occlusionShader.setUniform("pixelTexture", tileTexture);
        occlusionShader.setUniform("chacterPos", sf::Glsl::Vec2(characters[0]->x/screenW, characters[0]->y/screenH));
        occlusionRenderTexture.draw(tileSprite, &occlusionShader);

        occlusionShaderPost.setUniform("occlusionTexture", occlusionRenderTexture.getTexture());
        occlusionShaderPost.setUniform("chacterPos", sf::Glsl::Vec2(characters[0]->x/screenW, characters[0]->y/screenH));
        occlusionSpritePost.setPosition(mx, my);
        occlusionSpritePost.setScale(scaleX, scaleY);
        window.draw(occlusionSpritePost, &occlusionShaderPost);

        //occlusionSpriteDebug.setTexture(occlusionRenderTexture.getTexture(), true);
        //occlusionSpriteDebug.setPosition(mx+300, my);
        //occlusionSpriteDebug.setScale(scaleX, scaleY*20);
        //window.draw(occlusionSpriteDebug);
    }*/


    inline int mapX(int x, int screenW) {
        //int mw = w * scaleX;
        //int mx = screenW/2 - mw/2;
        x -= mx_;
        x /= scaleX;
        if(x < 0 || x > w-1) {
            return -1;
        }
        return x;
    }
    inline int mapY(int y, int screenH) {
        //int mh = h * scaleY;
        //int my = screenH/2 - mh/2;
        y -= my_;
        y /= scaleY;
        if(y < 0 || y > h-1) {
            return -1;
        }
        return y;
    }

    bool isTileWithin(int px, int py) {
        return px>=0 && px<w && py>=0 && py<h;
    }

    int mapInverseX(int px, int screenW) {
        if(px < 0 || px > w-1) {
            return -1;
        }
        return px*scaleX + (screenW/2 - w*scaleX/2);
    }
    int mapInverseY(int py, int screenH) {
        if(py < 0 || py > h-1) {
            return -1;
        }
        return py*scaleY + (screenH/2 - h*scaleY/2);
    }
    /*
    w = (y - (screenH/2 - w*scaleY/2)) / r
    w*r = y - (screenH/2 - w*scaleY/2)
    y = w*r + (screenH/2 - w*scaleY/2)
    */

    void hover(int px, int py, int screenW, int screenH) {
        hoverX = mapX(px, screenW);
        hoverY = mapY(py, screenH);
    }





    void click(bool button1, int px, int py, int screenW, int screenH) {
        if(gameStatus != GameStatus::Active) {
            return;
        }

        int x = mapX(px, screenW);
        int y = mapY(py, screenH);
        if(x == -1 || y == -1) {
            return;
        }

    }

    inline bool isWithinBounds(int x, int y) {
        return x >= 0 && x < w && y >= 0 && y < h;
    }




    void paintTilesNoMapping(TilePainter &tilePainter, int px, int py) {
        //int x = mapX(px, screenW);
        //int y = mapY(py, screenH);
        int x = px;
        int y = py;
        if(x == -1 || y == -1) {
            return;
        }

        if(tilePainter.painterType == TilePainter::Brush) {

            int sx = x - tilePainter.brushSize/2;
            int sy = y - tilePainter.brushSize/2;
            for(int i=0; i<tilePainter.brushSize; i++) {
                for(int j=0; j<tilePainter.brushSize; j++) {
                    int tx = sx + i;
                    int ty = sy + j;

                    if(isWithinBounds(tx, ty)) {
                        int ind = tx + ty*w;
                        //if(tiles[ind].type != tilePainter.type) {
                        if(tiles[ind].type != tilePainter.type || tiles[ind].flammable != tilePainter.flammable) {
                            tiles[ind].type = tilePainter.type;
                            tiles[ind].flammable = (tilePainter.flammable || tilePainter.type == Tile::Type::Oil) && tilePainter.type != Tile::Type::Water;
                            tiles[ind].burning = false;
                            tiles[ind].health = 1.0;

                            if(tiles[ind].type == Tile::Type::None) {
                                tiles[ind].color = getNoneTileColor();
                            }
                            else if(tiles[ind].type == Tile::Type::Ground) {
                                if(tilePainter.flammable) {// TODO update below...
                                    tiles[ind].color = getFlammableGroundTileColor(ind);
                                }
                                else {
                                    tiles[ind].color = getGroundTileColor(ind);
                                }
                            }
                            else if(tiles[ind].type == Tile::Type::Dirt) {
                                if(tilePainter.flammable) {
                                    tiles[ind].color = getFlammableDirtColor();
                                }
                                else {
                                    tiles[ind].color = getDirtColor();
                                }
                            }
                            else if(tiles[ind].type == Tile::Type::Water) {
                                tiles[ind].color = getWaterTileColor();
                            }
                            else if(tiles[ind].type == Tile::Type::Oil) {
                                tiles[ind].color = getOilTileColor();
                            }

                        }
                    }
                }
            }

        }
    }






    void paintTilesNoMappingLargeTiles(const LargeTileSet &largeTileSet, int largeTileSetIndex, bool flammable, int px, int py, int tx, int ty) {
        //int x = mapX(px, screenW);
        //int y = mapY(py, screenH);

        int mapInd = px + py*w;

        int tileInd = tx + ty * largeTileSet.largeTiles[largeTileSetIndex]->w;
        tileInd = tileInd * 4;

        tiles[mapInd].type = Tile::Type::Ground;
        tiles[mapInd].flammable = flammable;
        tiles[mapInd].health = randf2(1, 8);

        sf::Color color;
        color.r = largeTileSet.largeTiles[largeTileSetIndex]->pixels[tileInd + 0];
        color.g = largeTileSet.largeTiles[largeTileSetIndex]->pixels[tileInd + 1];
        color.b = largeTileSet.largeTiles[largeTileSetIndex]->pixels[tileInd + 2];
        color.a = largeTileSet.largeTiles[largeTileSetIndex]->pixels[tileInd + 3];

        tiles[mapInd].color = color;

        //if(tiles[ind].type != tilePainter.type) {
        /*if(tiles[ind].type != tilePainter.type || tiles[ind].flammable != tilePainter.flammable) {
            tiles[ind].type = tilePainter.type;
            tiles[ind].flammable = (tilePainter.flammable || tilePainter.type == Tile::Type::Oil) && tilePainter.type != Tile::Type::Water;
            tiles[ind].burning = false;
            tiles[ind].health = 1.0;*/

        

        /*if(tiles[ind].type == Tile::Type::None) {
            tiles[ind].color = getNoneTileColor();
        }
        else if(tiles[ind].type == Tile::Type::Ground) {
            if(tilePainter.flammable) {// TODO update below...
                tiles[ind].color = getFlammableGroundTileColor(ind);
            }
            else {
                tiles[ind].color = getGroundTileColor(ind);
            }
        }
        else if(tiles[ind].type == Tile::Type::Dirt) {
            if(tilePainter.flammable) {
                tiles[ind].color = getFlammableDirtColor();
            }
            else {
                tiles[ind].color = getDirtColor();
            }
        }
        else if(tiles[ind].type == Tile::Type::Water) {
            tiles[ind].color = getWaterTileColor();
        }
        else if(tiles[ind].type == Tile::Type::Oil) {
            tiles[ind].color = getOilTileColor();
        }*/

        //}
    }










    void paintTiles(TilePainter &tilePainter, int px, int py) {
        int x = mapX(px, screenW);
        int y = mapY(py, screenH);
        if(x == -1 || y == -1) {
            return;
        }

        if(tilePainter.painterType == TilePainter::Brush) {

            int sx = x - tilePainter.brushSize/2;
            int sy = y - tilePainter.brushSize/2;
            for(int i=0; i<tilePainter.brushSize; i++) {
                for(int j=0; j<tilePainter.brushSize; j++) {
                    int tx = sx + i;
                    int ty = sy + j;

                    if(isWithinBounds(tx, ty)) {
                        int ind = tx + ty*w;
                        //if(tiles[ind].type != tilePainter.type) {
                        if(tiles[ind].type != tilePainter.type || tiles[ind].flammable != tilePainter.flammable) {
                            tiles[ind].type = tilePainter.type;
                            tiles[ind].flammable = (tilePainter.flammable || tilePainter.type == Tile::Type::Oil) && tilePainter.type != Tile::Type::Water;
                            tiles[ind].burning = false;
                            tiles[ind].health = 1.0;

                            if(tiles[ind].type == Tile::Type::None) {
                                tiles[ind].color = getNoneTileColor();
                            }
                            else if(tiles[ind].type == Tile::Type::Ground) {
                                if(tilePainter.flammable) {
                                    tiles[ind].color = getFlammableGroundTileColor(ind);
                                }
                                else {
                                    tiles[ind].color = getGroundTileColor(ind);
                                }
                            }
                            else if(tiles[ind].type == Tile::Type::Dirt) {
                                if(tilePainter.flammable) {
                                    tiles[ind].color = getFlammableDirtColor();
                                }
                                else {
                                    tiles[ind].color = getDirtColor();
                                }
                            }
                            else if(tiles[ind].type == Tile::Type::Water) {
                                tiles[ind].color = getWaterTileColor();
                            }
                            else if(tiles[ind].type == Tile::Type::Oil) {
                                tiles[ind].color = getOilTileColor();
                            }

                        }
                    }
                }
            }

        }
    }





    /*void paintTilesNoMapping(TilePainter &tilePainter, int px, int py) {
        //int x = mapX(px, screenW);
        //int y = mapY(py, screenH);
        int x = px;
        int y = py;
        if(x == -1 || y == -1) {
            return;
        }

        if(tilePainter.painterType == TilePainter::Brush) {

            int sx = x - tilePainter.brushSize/2;
            int sy = y - tilePainter.brushSize/2;
            for(int i=0; i<tilePainter.brushSize; i++) {
                for(int j=0; j<tilePainter.brushSize; j++) {
                    int tx = sx + i;
                    int ty = sy + j;

                    if(isWithinBounds(tx, ty)) {
                        int ind = tx + ty*w;
                        //if(tiles[ind].type != tilePainter.type) {
                        if(tiles[ind].type != tilePainter.type || tiles[ind].flammable != tilePainter.flammable) {
                            tiles[ind].type = tilePainter.type;
                            tiles[ind].flammable = (tilePainter.flammable || tilePainter.type == Tile::Type::Oil) && tilePainter.type != Tile::Type::Water;
                            tiles[ind].burning = false;
                            tiles[ind].health = 1.0;

                            if(tiles[ind].type == Tile::Type::None) {
                                tiles[ind].color = sf::Color(noneR, noneG, noneB, tileAlpha);
                            }
                            else if(tiles[ind].type == Tile::Type::Ground) {
                                if(tilePainter.flammable) {// TODO update below...
                                    tiles[ind].color = sf::Color(randi(140, 160), randi(100, 120), randi(60, 80), tileAlpha);
                                }
                                else {
                                    int c = randi(30, 220);
                                    tiles[ind].color = sf::Color(c, c, c, tileAlpha);
                                }
                            }
                            else if(tiles[ind].type == Tile::Type::Dirt) {
                                if(tilePainter.flammable) {
                                    tiles[ind].color = sf::Color(randi(130, 150), randi(90, 110), randi(50, 70), tileAlpha);
                                }
                                else {
                                    tiles[ind].color = sf::Color(randi(185, 220), randi(155, 185), randi(110, 130), tileAlpha);
                                }
                            }
                            else if(tiles[ind].type == Tile::Type::Water) {
                                tiles[ind].color = sf::Color(randi(90, 110), randi(90, 110), randi(220, 255), 150);
                            }
                            else if(tiles[ind].type == Tile::Type::Oil) {
                                tiles[ind].color = sf::Color(randi(80, 100), randi(70, 90), randi(40, 60), 150);
                            }

                        }
                    }
                }
            }

        }
    }

    void paintTiles(TilePainter &tilePainter, int px, int py) {
        int x = mapX(px, screenW);
        int y = mapY(py, screenH);
        if(x == -1 || y == -1) {
            return;
        }

        if(tilePainter.painterType == TilePainter::Brush) {

            int sx = x - tilePainter.brushSize/2;
            int sy = y - tilePainter.brushSize/2;
            for(int i=0; i<tilePainter.brushSize; i++) {
                for(int j=0; j<tilePainter.brushSize; j++) {
                    int tx = sx + i;
                    int ty = sy + j;

                    if(isWithinBounds(tx, ty)) {
                        int ind = tx + ty*w;
                        //if(tiles[ind].type != tilePainter.type) {
                        if(tiles[ind].type != tilePainter.type || tiles[ind].flammable != tilePainter.flammable) {
                            tiles[ind].type = tilePainter.type;
                            tiles[ind].flammable = (tilePainter.flammable || tilePainter.type == Tile::Type::Oil) && tilePainter.type != Tile::Type::Water;
                            tiles[ind].burning = false;
                            tiles[ind].health = 1.0;

                            if(tiles[ind].type == Tile::Type::None) {
                                tiles[ind].color = sf::Color(noneR, noneG, noneB, tileAlpha);
                            }
                            else if(tiles[ind].type == Tile::Type::Ground) {
                                if(tilePainter.flammable) {
                                    tiles[ind].color = sf::Color(randi(110, 130), randi(70, 90), randi(30, 50), tileAlpha);
                                }
                                else {
                                    int c = randi(30, 220);
                                    tiles[ind].color = sf::Color(c, c, c, tileAlpha);
                                }
                            }
                            else if(tiles[ind].type == Tile::Type::Dirt) {
                                if(tilePainter.flammable) {
                                    tiles[ind].color = sf::Color(randi(130, 150), randi(90, 110), randi(50, 70), tileAlpha);
                                }
                                else {
                                    tiles[ind].color = sf::Color(randi(185, 220), randi(155, 185), randi(110, 130), tileAlpha);
                                }
                            }
                            else if(tiles[ind].type == Tile::Type::Water) {
                                tiles[ind].color = sf::Color(randi(90, 110), randi(90, 110), randi(220, 255), 150);
                            }
                            else if(tiles[ind].type == Tile::Type::Oil) {
                                tiles[ind].color = sf::Color(randi(80, 100), randi(70, 90), randi(40, 60), 150);
                            }

                        }
                    }
                }
            }

        }
    }*/

    void drag(bool button1, int px, int py, int screenW, int screenH) {
        if(gameStatus != GameStatus::Active) {
            return;
        }
        paintTiles(this->tilePainter, px, py);
    }

    enum CollisionTileType { Firm, All };

    bool checkCollision(float x, float y, CollisionTileType collisionTileType = CollisionTileType::Firm) {
        /*float mw = w * scaleX;
        float mh = h * scaleY;
        float mx = screenW/2 - mw/2;
        float my = screenH/2 - mh/2;*/

        if(x >= mx_ + mw_) {
            return true;
        }
        else if(x < mx_) {
            return true;
        }
        else if(y > my_ + mh_) {
            return true;
        }
        else if(y < my_) {
            return true;
        }
        else {
            int px = mapX(x, screenW);
            int py = mapY(y, screenH);
            if(px != -1 && py != -1) {
                if(collisionTileType == CollisionTileType::Firm) {
                    if(tiles[px + py*w].type == Map::Tile::Ground || tiles[px + py*w].type == Map::Tile::Dirt) {
                        return true;
                    }
                }
                else if(collisionTileType == CollisionTileType::All) {
                    if(tiles[px + py*w].type != Map::Tile::None) {
                        return true;
                    }
                }
            }
        }
        return false;
    }



    bool getCollisionReflection(float x, float y, float dx, float dy, float &rx, float &ry, int tileCheckingRadius = 4) {

        /*float mw = w * scaleX;
        float mh = h * scaleY;
        float mx = screenW/2 - mw/2;
        float my = screenH/2 - mh/2;*/

        /*float distd = sqrt(dx*dx + dy*dy);
        dx = dx/distd;
        dy = dy/distd;*/

        if(x >= mx_ + mw_) {
            float nx = 1;
            float ny = 0;
            reflectVector(dx, dy, nx, ny, rx, ry);
            return true;
        }
        else if(x < mx_) {
            float nx = 1;
            float ny = 0;
            reflectVector(dx, dy, nx, ny, rx, ry);
            return true;
        }
        else if(y > my_ + mh_) {
            float nx = 0;
            float ny = 1;
            reflectVector(dx, dy, nx, ny, rx, ry);
            return true;
        }
        else if(y < my_) {
            float nx = 0;
            float ny = 1;
            reflectVector(dx, dy, nx, ny, rx, ry);
            return true;
        }
        else {
            int px = mapX(x, screenW);
            int py = mapY(y, screenH);
            if(px != -1 && py != -1) {
                //if(tiles[px + py*w].type != Map::Tile::None) {
                if(tiles[px + py*w].type == Map::Tile::Ground || tiles[px + py*w].type == Map::Tile::Dirt) {
                    float rr = tileCheckingRadius*tileCheckingRadius;
                    float nx = 0, ny = 0;
                    int nn = 0;
                    for(int tx=-tileCheckingRadius; tx<tileCheckingRadius; tx++) {
                        for(int ty=-tileCheckingRadius; ty<tileCheckingRadius; ty++) {
                            if(!isTileWithin(px+tx, py+ty)) {
                                continue;
                            }
                            float trr = tx*tx + ty*ty;
                            //if(trr < rr && tiles[px+tx + (py+ty)*w].type != Map::Tile::None) {
                            if(trr < rr && (tiles[px+tx + (py+ty)*w].type == Map::Tile::Ground || tiles[px+tx + (py+ty)*w].type == Map::Tile::Dirt)) {
                                //map.tiles[px+tx + (py+ty)*map.w].color = sf::Color(255, 255, 0, 255);
                                float rnx = tx;
                                float rny = ty;
                                float dist = sqrt(rnx*rnx + rny*rny);
                                if(dist != 0) {
                                    rnx /= dist;
                                    rny /= dist;
                                    nn++;
                                    nx += rnx;
                                    ny += rny;
                                }
                            }
                        }
                    }
                    float distn = sqrt(nx*nx + ny*ny);
                    if(distn == 0) distn = 1;
                    nx /= distn;
                    ny /= distn;

                    //reflectVector(dx/distd, dy/distd, ny, -nx, rx, ry);
                    reflectVector(dx, dy, nx, ny, rx, ry);
                    return true;
                }
            }
        }
        return false;
    }



};






















struct Item {
    vector<Character*> characters;      // TODO remove this
    Character* itemUserCharacter = nullptr;
    Vehicle* itemUserVehicle = nullptr;

    float itemMana = 0;

    int team = 0;
    bool ignoreTeamMates = false;

    virtual ~Item() {}
    void addCharacter(Character *character) {
        characters.push_back(character);
    }
    virtual void use(float x, float y, float vx, float vy, float angle) {
        printf("Item::Use()\n");
    }
    virtual void render(sf::RenderWindow &window) {}
    virtual void update(Map &map, float dt) {}
    virtual void afterUpdate(bool forceInactive = false) {}
    virtual string getName() {return "";}
    virtual void updateNonActive(Map &map, float dt) {}

    virtual void loadMana(Map &map, float dt) {
        if(loadingTimeSeconds() == 0) {
            itemMana = 100; // TODO character max mana
        }
        else {
            itemMana += dt * 100.0 / loadingTimeSeconds();
            if(itemMana > 100) {
                itemMana = 100;
            }
        }
    }

    virtual float manaCostPerUse() {return 0;}
    virtual bool noManaCostPerUse() {return false;}
    virtual float manaCostPerSecond() {return 0;}
    virtual float loadingTimeSeconds() {return 0;}
    virtual float repeatTime() {return 0;}
};








struct FlyingTile {
    Map::Tile tile;
    FlyingTile() {
        tile.wasFlying = true;
    }
    //float x = 0, float y = 0;
    //float vx = 0, float vy = 0;
};
















struct Smoke : public Projectile {
    static sf::RectangleShape rect;
    sf::Color color;
    sf::Color color2;

    float time = 0;
    float duration = 3;

    static void prepare() {

        rect = sf::RectangleShape(sf::Vector2f(1, 1));
        rect.setFillColor(sf::Color(255, 255, 255, 255));
        rect.setScale(scaleX, scaleY);
    }

    void update(Map& map, float dt) {
        if(gravityHasAnEffect) {
            vy += gravity * dt;
        }
        x += vx * dt;
        y += vy * dt;

        time += dt;

        if(time >= duration) {
            canBeRemoved = true;
            return;
        }
    }

    void render(sf::RenderWindow &window, float scaleX, float scaleY) {
        rect.setPosition(x, y);
        //rect.setScale(scaleX, scaleY);

        sf::Color c = mix(color, color2, time/duration);
        rect.setFillColor(c);

        window.draw(rect);

        printf("Rendering smoke???\n");
    }

    sf::Color getPixelColor() {

        return mix(color, color2, time/duration);
        //return sf::Color(255, 0, 255, 255);
    }
    virtual std::string getName() {
        return "Smoke";
    }
};
sf::RectangleShape Smoke::rect;



std::mutex addProjectileMutex;
//std::mutex createSmokeMutex;
//std::mutex createFlameMutex;


void createSmoke(float x, float y, vector<Character*> &characters, int numSmokeProjectiles, float smokeProjectileVelocityMin, float smokeProjectileVelocityMax, float initialRadius) {

    for(int i=0; i<numSmokeProjectiles; i++) {
        Smoke *smokeProjectile = new Smoke();
        float angle = randf2(-Pi*0.45, -Pi*0.55);
        float v = randf2(smokeProjectileVelocityMin, smokeProjectileVelocityMax);
        smokeProjectile->vx = v * cos(angle);
        smokeProjectile->vy = v * sin(angle);
        float angle2 = randf2(0, 2.0*Pi);
        float ra = randf2(0, initialRadius);
        smokeProjectile->x = x + ra * cos(angle2);
        smokeProjectile->y = y + ra * sin(angle2);
        smokeProjectile->gravityHasAnEffect = false;
        int c = randi2(50, 255);
        int alpha = randi2(50, 255);
        smokeProjectile->color = sf::Color(c, c, c, alpha);
        smokeProjectile->color2 = sf::Color(0, 0, 0, 0);

        smokeProjectile->duration = randf2(0.1, 1);
        smokeProjectile->characters = characters;
        //smokeProjectile->projectileUserCharacter = characters[0];
        addProjectileMutex.lock();
        projectiles.push_back(smokeProjectile);
        addProjectileMutex.unlock();
    }

}



struct Flame : public Projectile {

    static sf::RectangleShape rect;
    sf::Color color;
    sf::Color color2;

    float time = 0;
    float duration = 3;
    float dealDamagePerSecond = 0;

    static void prepare() {
        rect = sf::RectangleShape(sf::Vector2f(1, 1));
        rect.setFillColor(sf::Color(255, 255, 255, 255));
        rect.setScale(scaleX, scaleY); // TODO fix
    }

    void update(Map& map, float dt);


    void render(sf::RenderWindow &window, float scaleX, float scaleY) {
        rect.setPosition(x, y);
        //rect.setScale(scaleX, scaleY);

        sf::Color c = mix(color, color2, time/duration);
        rect.setFillColor(c);

        window.draw(rect);

    }

    sf::Color getPixelColor() {
        return mix(color, color2, time/duration);
    }

    virtual std::string getName() {
        return "Flame";
    }
};
sf::RectangleShape Flame::rect;



void createFlame(float x, float y, vector<Character*> &characters, int numFlameProjectiles, float flameProjectileVelocityMin, float flameProjectileVelocityMax, float dealDamagePerSecond) {

    for(int i=0; i<numFlameProjectiles; i++) {
        Flame *flameProjectile = new Flame();
        float angle = randf2(-Pi*0.45, -Pi*0.55);
        float v = randf2(flameProjectileVelocityMin, flameProjectileVelocityMax);
        flameProjectile->vx = v * cos(angle);
        flameProjectile->vy = v * sin(angle);
        float angle2 = randf2(0, 2.0*Pi);
        float ra = randf2(0, 5);
        flameProjectile->x = x + ra * cos(angle2);
        flameProjectile->y = y + ra * sin(angle2);
        flameProjectile->gravityHasAnEffect = false;
        flameProjectile->dealDamagePerSecond = dealDamagePerSecond;

        int g = randi2(0, 255);
        int r = randi2(g, 255);
        int b = 0;
        int alpha = randi2(50, 255);

        flameProjectile->color = sf::Color(r, g, b, alpha);
        flameProjectile->color2 = sf::Color(0, 0, 0, 0);
        flameProjectile->duration = randf2(0.1, 1);
        flameProjectile->characters = characters;
        //flameProjectile->projectileUserCharacter = itemUserCharacter;
        addProjectileMutex.lock();
        projectiles.push_back(flameProjectile);
        addProjectileMutex.unlock();
    }

}



struct ExplosionProjectile : public Projectile {

    enum class Type { Basic, BasicWithCollision, FlyingTile };
    enum Direction { Up, Down, Left, Right };
    Direction direction = Up;

    static sf::RectangleShape rect;

    sf::Color color;
    sf::Color color2;

    //bool isTile = false;
    Type type = Type::Basic;
    //Tile tile;
    FlyingTile flyingTile;

    float time = 0;
    float duration = 100;


    bool isNuclear = false;

    bool createSmokeTrail = false;

    float dealDamage = 0;

    static void prepare() {
        rect = sf::RectangleShape(sf::Vector2f(1, 1));
        rect.setFillColor(sf::Color(255, 255, 255, 255));
    }


    /*void update(Map& map, float dt) {
       float k = round(max(1+abs(vx) * dt, 1+abs(vy) * dt));
        k = min(k, 500);
        if(k == 0) k = 1;
        float _dt = dt/k;
        
        if(createSmokeTrail) {
            createSmoke(x, y, map.characters, 1, 20, 200);
            createFlame(x, y, map.characters, 1, 20, 200);
        }

        for(int i=0; i<k; i++) {
            if(gravityHasAnEffect) {
                vy += gravity * _dt;
            }
            x += vx * _dt;
            y += vy * _dt;

            _update(map, _dt);
        }
    }

    void _update(Map& map, float dt);*/

    void update(Map& map, float dt);


    void render(sf::RenderWindow &window, float scaleX, float scaleY) {
        rect.setPosition(x, y);
        rect.setScale(scaleX, scaleY);
        if(type == Type::FlyingTile) {
            if(flyingTile.tile.burning) {
                int g = randi(0, 255);
                int r = randi(g, 255);
                int b = 0;
                int a = 255;
                rect.setFillColor(sf::Color(r, g, b, a));
            }
            else {
                rect.setFillColor(flyingTile.tile.color);
            }
        }
        else {
            sf::Color c = mix(color, color2, time/duration);

            rect.setFillColor(c);
        }
        window.draw(rect);
    }

    sf::Color getPixelColor() {
        if(type == Type::FlyingTile && !isNuclear) {
            if(flyingTile.tile.burning) {
                int g = randi(0, 255);
                int r = randi(g, 255);
                int b = 0;
                int a = 255;
                return sf::Color(r, g, b, a);
            }
            else {
                return flyingTile.tile.color;
            }
        }
        else {
            return mix(color, color2, time/duration);
        }
    }

    virtual std::string getName() {
        return "Explosion Projectile";
    }
};
sf::RectangleShape ExplosionProjectile::rect;







struct ExplosionProjectile2 : public Projectile {
    sf::Color color1, color2, color3;
    float time = 0;
    float duration1 = 1, duration2 = 1;
    float risingAcceleration = 0;

    virtual void update(Map &map, float dt) {
        if(gravityHasAnEffect) {
            vy += gravity * dt;
        }

        vy -= dt * risingAcceleration;

        vx = vx * 0.98;
        vy = vy * 0.94;

        x += vx * dt;
        y += vy * dt;

        time += dt;

        if(time >= duration1 + duration2) {
            canBeRemoved = true;
            return;
        }
    }

    virtual sf::Color getPixelColor() {
        if(time < duration1) {
            return mix(color1, color2, time/duration1);
        }
        else {
            return mix(color2, color3, (time-duration1)/duration2);
        }
    }

    virtual std::string getName() {
        return "Explosion Projectile 2";
    }
};


void createExplosion2(float x, float y, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {
    for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile2 *p = new ExplosionProjectile2();
        float d = randf2(0, explosionRadius);
        float a1 = randf2(0, 2.0*Pi);
        p->x = x + d * cos(a1);
        p->y = y + d * sin(a1);

        float v = randf2(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        float a2 = randf2(0, 2.0*Pi);
        p->vx = v * cos(a2);
        p->vy = v * sin(a2);

        float alpha1 = randf2(150, 255);
        float r1 = randf2(50, 255);
        float g1 = randf2(0, r1);
        //float g1 = 255 - randf2(0, mapf(d, 0, explosionRadius, 0, 255));
        float b1 = 0;//randf2(0, g1*0.5);
        p->color1 = sf::Color(r1, g1, b1, alpha1);

        float c = randf2(50, 150);
        float alpha2 = randf2(100, 150);
        p->color2 = sf::Color(c, c, c, alpha2);

        float c2 = randf2(0, 50);
        float alpha3 = randf2(0, 50);
        p->color3 = sf::Color(c2, c2, c2, alpha3);

        p->duration1 = randf2(0.5, 1.0);
        p->duration2 = randf2(1.0, 2.0);
        
        p->gravityHasAnEffect = false;
        p->risingAcceleration = randf2(50, 500);

        projectiles.push_back(p);
    }
}





struct Bomb : public Item {

    /*struct BombExplosionProjectile : public Projectile {
        sf::Color color1, color2, color3;
        float time = 0;
        float duration1 = 1, duration2 = 1;
        float risingAcceleration = 0;

        virtual void update(Map &map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }

            vy -= dt * risingAcceleration;

            vx = vx * 0.98;
            vy = vy * 0.98;

            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration1 + duration2) {
                canBeRemoved = true;
                return;
            }
        }

        virtual sf::Color getPixelColor() {
            if(time < duration1) {
                return mix(color1, color2, time/duration1);
            }
            else {
                return mix(color2, color3, (time-duration1)/duration2);
            }
        }

        virtual std::string getName() {
            return "Bomb Explosion Projectile";
        }
    };*/


    struct BombProjectile : public Projectile {
        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::SoundBuffer explosionSoundBuffer;

        static void prepare() {
            if(!image.loadFromFile("data/textures/bomb.png")) {
                printf("Couldn't open file 'data/textures/bomb.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/explo12.ogg")) {
                printf("Couldn't open file 'data/audio/explo12.ogg'!\n");
                return;
            }
        }

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);


        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool exploded = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;

                exploded = exploded || _update(map, _dt, exploded);
            }
        }

        bool _update(Map& map, float dt, bool exploded = false);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Bomb Projectile";
        }
    };


    static constexpr float throwingVelocity = 1000;



    Bomb() {}
    ~Bomb() {}

    void use(float x, float y, float vx, float vy, float angle);

    string getName() {
        return "Bomb";
    }

    float manaCostPerUse() {
        return 100.0/6.0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0.25;
    }

};

sf::Image Bomb::BombProjectile::image;
sf::Texture Bomb::BombProjectile::texture;
sf::Sprite Bomb::BombProjectile::sprite;

sf::SoundBuffer Bomb::BombProjectile::explosionSoundBuffer;












struct ClusterBomb : public Item {

    struct ClusterBombProjectile : public Projectile {
        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static void prepare() {
            if(!image.loadFromFile("data/textures/bomb.png")) {
                printf("Couldn't open file 'data/textures/bomb.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);
        }

        void launchClusters(float vx, float vy) {
            for(int i=0; i<10; i++) {
                ClusterProjectile *clusterProjectile = new ClusterProjectile();
                clusterProjectile->x = x;
                clusterProjectile->y = y;
                float a = randf(Pi, 2.0*Pi);
                float v = randf(50, 300);
                clusterProjectile->vx = vx + v * cos(a);
                clusterProjectile->vy = vy + v * sin(a);
                clusterProjectile->gravityHasAnEffect = true;
                //clusterProjectile->characters = characters;
                clusterProjectile->projectileUserCharacter = this->projectileUserCharacter;
                clusterProjectile->projectileUserVehicle = this->projectileUserVehicle;
                projectiles.push_back(clusterProjectile);
            }
        }

        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;

                _update(map, _dt);
            }
        }

        void _update(Map& map, float dt) {

            //float m = max(vx * dt, vy * dt);
            /*if(m > 1) {
                printf("m %f\n", m);
            }*/

            float mw = map.w * map.scaleX;
            float mh = map.h * map.scaleY;
            float mx = screenW/2 - mw/2;
            float my = screenH/2 - mh/2;


            if(x >= mx + mw) {
                vx = 0;
                vy = 0;
                gravityHasAnEffect = false;
            }
            else if(x < mx) {
                vx = 0;
                vy = 0;
                gravityHasAnEffect = false;
            }
            else if(y > my + mh) {
                vx = 0;
                vy = 0;
                gravityHasAnEffect = false;
            }
            else if(y < my) {
                vx = 0;
                vy = 0;
                gravityHasAnEffect = false;
            }

            int px = map.mapX(x, screenW);
            int py = map.mapY(y, screenH);
            if(px != -1 && py != -1) {
                bool collided = false;
                if(map.tiles[px + py*map.w].type == Map::Tile::Ground || map.tiles[px + py*map.w].type == Map::Tile::Dirt) {
                    vx = 0;
                    vy = 0;
                    gravityHasAnEffect = false;
                }
            }
        }

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }
        virtual std::string getName() {
            return "Cluster Bomb Projectile";
        }
    };




    struct ClusterProjectile : public Projectile {
        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::SoundBuffer explosionSoundBuffer;

        static void prepare() {
            if(!image.loadFromFile("data/textures/cluster.png")) {
                printf("Couldn't open file 'data/textures/cluster.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/explo12c.ogg")) {
                printf("Couldn't open file 'data/audio/explo12c.ogg'!\n");
                return;
            }
        }

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);


        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            //printf("k %f, _dt %f\n", k, _dt);
            bool exploded = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;

                exploded = exploded || _update(map, _dt, exploded);
                //_update(map, _dt);
            }
        }

        bool _update(Map& map, float dt, bool exploded = false); /*{


            if(exploded) return exploded;

            float m = max(vx * dt, vy * dt);
            if(m > 1) {
                printf("m %f\n", m);
            }

            //sf::Vector2u size = image.getSize();
            float mw = map.w * map.scaleX;
            float mh = map.h * map.scaleY;
            float mx = screenW/2 - mw/2;
            float my = screenH/2 - mh/2;


            if(x >= mx + mw) {
                createExplosion(map, mx + mw - 1, y, characters, 40, 500, 0, 200);
                exploded = true;
                canBeRemoved = true;
            }
            else if(x < mx) {
                createExplosion(map, mx + 1, y, characters, 40, 500, 0, 200);
                exploded = true;
                canBeRemoved = true;
            }
            else if(y > my + mh) {
                createExplosion(map, x, my + mh - 1, characters, 40, 500, 0, 200);
                exploded = true;
                canBeRemoved = true;
            }
            else if(y < my) {
                createExplosion(map, x, my + 1, characters, 40, 500, 0, 200);
                exploded = true;
                canBeRemoved = true;
            }

            int px = map.mapX(x, screenW);
            int py = map.mapY(y, screenH);
            if(px != -1 && py != -1) {
                bool collided = false;
                if(map.tiles[px + py*map.w].type == Map::Tile::Ground || map.tiles[px + py*map.w].type == Map::Tile::Dirt) {

                    createExplosion(map, x, y, characters, 40, 500, 0, 200);
                    exploded = true;
                    canBeRemoved = true;
                }
            }
            return exploded;
        }*/

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Cluster Projectile";
        }
    };





    ClusterBombProjectile *clusterBombProjectile = nullptr;

    float throwingVelocity = 1000;

    /*float explosionRadius = 10;
    int numExplosionProjectiles = 100;

    float explosionProjectileVelocityMin = 100;
    float explosionProjectileVelocityMax = 1500;*/

    bool bombThrown = false;

    ClusterBomb() {}
    ~ClusterBomb() {}

    void use(float x, float y, float vx, float vy, float angle) {

        if(!bombThrown) {
            clusterBombProjectile = new ClusterBombProjectile();
            clusterBombProjectile->x = x;
            clusterBombProjectile->y = y;
            clusterBombProjectile->vx = throwingVelocity * cos(angle);
            clusterBombProjectile->vy = throwingVelocity * sin(angle);
            clusterBombProjectile->gravityHasAnEffect = true;
            //clusterBombProjectile->characters = characters;
            clusterBombProjectile->projectileUserCharacter = itemUserCharacter;
            clusterBombProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(clusterBombProjectile);
            bombThrown = true;
        }
        else {
            if(clusterBombProjectile) {
                clusterBombProjectile->launchClusters(clusterBombProjectile->vx, clusterBombProjectile->vy);

                //delete clusterBombProjectile;
                clusterBombProjectile->canBeRemoved = true;
                clusterBombProjectile = nullptr;
            }
            bombThrown = false;
        }
    }

    string getName() {
        return "Cluster Bomb";
    }

    float manaCostPerUse() {
        //return bombThrown ? 100.0/2.0 : 0;
        return 100.0/4.0;
    }

    bool noManaCostPerUse() {
        return bombThrown;
    }


    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        if(bombThrown) {
            return 0.25;
        }
        else {
            return 0.5;
        }
    }


};

sf::Image ClusterBomb::ClusterBombProjectile::image;
sf::Texture ClusterBomb::ClusterBombProjectile::texture;
sf::Sprite ClusterBomb::ClusterBombProjectile::sprite;

sf::Image ClusterBomb::ClusterProjectile::image;
sf::Texture ClusterBomb::ClusterProjectile::texture;
sf::Sprite ClusterBomb::ClusterProjectile::sprite;

sf::SoundBuffer ClusterBomb::ClusterProjectile::explosionSoundBuffer;











struct Napalm : public Item {

    struct NapalmProjectile : public Projectile {
        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::SoundBuffer explosionSoundBuffer;

        static void prepare() {
            if(!image.loadFromFile("data/textures/napalm.png")) {
                printf("Couldn't open file 'data/textures/napalm.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/noise12.ogg")) {
                printf("Couldn't open file 'data/audio/noise12.ogg'!\n");
                return;
            }
            /*if(!explosionSoundBuffer.loadFromFile("data/audio/explo6.ogg")) {
                printf("Couldn't open file 'data/audio/explo6.ogg'!\n");
                return;
            }*/
        }


        void launch(float x, float y, float vx, float vy, vector<Character*> &characters, float explosionRadius, int numProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);

 
        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;

                _update(map, _dt);
            }
        }

        void _update(Map& map, float dt) {

            //float m = max(vx * dt, vy * dt);
            /*if(m > 1) {
                printf("m %f\n", m);
            }*/

            float mw = map.w * map.scaleX;
            float mh = map.h * map.scaleY;
            float mx = screenW/2 - mw/2;
            float my = screenH/2 - mh/2;


            if(x >= mx + mw) {
                vx = 0;
                vy = 0;
                gravityHasAnEffect = false;
            }
            else if(x < mx) {
                vx = 0;
                vy = 0;
                gravityHasAnEffect = false;
            }
            else if(y > my + mh) {
                vx = 0;
                vy = 0;
                gravityHasAnEffect = false;
            }
            else if(y < my) {
                vx = 0;
                vy = 0;
                gravityHasAnEffect = false;
            }

            int px = map.mapX(x, screenW);
            int py = map.mapY(y, screenH);
            if(px != -1 && py != -1) {
                bool collided = false;
                if(map.tiles[px + py*map.w].type == Map::Tile::Ground || map.tiles[px + py*map.w].type == Map::Tile::Dirt) {
                    vx = 0;
                    vy = 0;
                    gravityHasAnEffect = false;
                }
            }
        }

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Napalm Projectile";
        }
    };




    struct NapalmFireProjectile : public Projectile {

        //enum class Type { Basic, FlyingTile };
        enum Direction { Up, Down, Left, Right };
        Direction direction = Up;

        static sf::RectangleShape rect;

        sf::Color color;
        sf::Color color2;

        //bool isTile = false;
        //Type type = Type::Basic;
        //Tile tile;
        FlyingTile flyingTile;

        float time = 0;
        float duration = 100;



        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
        }

        /*void update(Map& map, float dt) {
            int k = round(max(1+vx * dt, 1+vy * dt));
            float _dt = dt/k;

            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;

                _update(map, _dt);
            }
        }*/

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            //printf("createSmoke(x, y, map.characters, 100, 20, 200);\n");

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }

            if(randf2(0, 1) < 0.2) {
                createSmoke(x, y, map.characters, 1, 20, 200);
                createFlame(x, y, map.characters, 1, 20, 200, 1);
            }

            //sf::Vector2u size = image.getSize();
            float mw = map.w * map.scaleX;
            float mh = map.h * map.scaleY;
            float mx = screenW/2 - mw/2;
            float my = screenH/2 - mh/2;

            if(x >= mx + mw) {
                //if(type == Type::FlyingTile) {
                    int px = map.mapX(mx + mw - 1, screenW);
                    int py = map.mapY(y, screenH);

                    if(px != -1 && py != -1) {
                        if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                            map.tiles[px + py*map.w] = flyingTile.tile;
                        }
                    }
                //}

                canBeRemoved = true;
            }
            else if(x < mx) {
                //if(type == Type::FlyingTile) {
                    int px = map.mapX(mx+1, screenW);
                    int py = map.mapY(y, screenH);
                    if(px != -1 && py != -1) {
                        if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                            map.tiles[px + py*map.w] = flyingTile.tile;
                        }
                    }
                //}
                canBeRemoved = true;
            }
            else if(y > my + mh) {
                //if(type == Type::FlyingTile) {
                    int px = map.mapX(x, screenW);
                    int py = map.mapY(my + mh - 1, screenH);

                    if(px != -1 && py != -1) {
                        if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                            map.tiles[px + py*map.w] = flyingTile.tile;
                        }
                    }
                //}
                canBeRemoved = true;
            }
            else if(y < my) {
                //if(type == Type::FlyingTile) {
                    int px = map.mapX(x, screenW);
                    int py = map.mapY(my+1, screenH);;

                    if(px != -1 && py != -1) {
                        if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                            map.tiles[px + py*map.w] = flyingTile.tile;
                        }
                    }
                //}
                canBeRemoved = true;
            }
            else {
                int px = map.mapX(x, screenW);
                int py = map.mapY(y, screenH);
                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type != Map::Tile::None) {
                        //if(type == Type::FlyingTile) {
                            int distUp = 0, distDown = 0, distLeft = 0, distRight = 0;

                            while(map.isTileWithin(px, py-distUp) && map.tiles[px + (py-distUp)*map.w].type != Map::Tile::None) {
                                distUp++;
                            }
                            while(map.isTileWithin(px, py+distDown) && map.tiles[px + (py+distDown)*map.w].type != Map::Tile::None) {
                                distDown++;
                            }
                            while(map.isTileWithin(px-distLeft, py) && map.tiles[px-distLeft + py*map.w].type != Map::Tile::None) {
                                distLeft++;
                            }
                            while(map.isTileWithin(px+distRight, py) && map.tiles[px-distLeft + py*map.w].type != Map::Tile::None) {
                                distRight++;
                            }

                            /*
                            while(map.isTileWithin(px, py-distUp) && (map.tiles[px + (py-distUp)*map.w].type == Map::Tile::Ground || map.tiles[px + (py-distUp)*map.w].type == Map::Tile::Dirt)) {
                                distUp++;
                            }
                            while(map.isTileWithin(px, py+distDown) && (map.tiles[px + (py+distDown)*map.w].type == Map::Tile::Ground || map.tiles[px + (py+distDown)*map.w].type == Map::Tile::Dirt)) {
                                distDown++;
                            }
                            while(map.isTileWithin(px-distLeft, py) && (map.tiles[px-distLeft + py*map.w].type == Map::Tile::Ground || map.tiles[px-distLeft + py*map.w].type == Map::Tile::Dirt)) {
                                distLeft++;
                            }
                            while(map.isTileWithin(px+distRight, py) && (map.tiles[px+distRight + py*map.w].type == Map::Tile::Ground || map.tiles[px+distRight + py*map.w].type == Map::Tile::Dirt)) {
                                distRight++;
                            }
                            */
                            if(distLeft <= distUp && distLeft <= distDown && distLeft <= distRight) {
                                direction = Direction::Left;
                            }
                            else if(distRight <= distUp && distLeft <= distDown && distRight <= distLeft) {
                                direction = Direction::Right;
                            }
                            else if(distDown <= distUp) {
                                direction = Direction::Down;
                            }
                            else {
                                direction = Direction::Up;
                            }

                            if(direction == Direction::Up) {
                                if(map.isTileWithin(px, py-distUp)) {
                                    map.tiles[px + (py-distUp)*map.w] = flyingTile.tile;
                                }

                            }
                            else if(direction == Direction::Down) {
                                if(map.isTileWithin(px, py+distDown)) {
                                    map.tiles[px + (py+distDown)*map.w] = flyingTile.tile;
                                }
                            }
                            if(direction == Direction::Left) {
                                if(map.isTileWithin(px-distLeft, py)) {
                                    map.tiles[px-distLeft + py*map.w] = flyingTile.tile;
                                }

                            }
                            else if(direction == Direction::Right) {
                                if(map.isTileWithin(px+distRight, py)) {
                                    map.tiles[px+distRight + py*map.w] = flyingTile.tile;
                                }
                            }


                        //}
                        canBeRemoved = true;
                    }
                }
            }

        }

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            rect.setScale(scaleX, scaleY);
            //if(type == Type::FlyingTile) {
                if(flyingTile.tile.burning) {
                    int g = randi(0, 255);
                    int r = randi(g, 255);
                    int b = 0;
                    int a = 255;
                    rect.setFillColor(sf::Color(r, g, b, a));
                }
                else {
                    rect.setFillColor(flyingTile.tile.color);
                }
            //}
            /*else {
                sf::Color c = mix(color, color2, time/duration);

                rect.setFillColor(c);
            }*/
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            int g = randi(0, 255);
            int r = randi(g, 255);
            int b = 0;
            int a = 255;
            return sf::Color(r, g, b, a);
        }

        virtual std::string getName() {
            return "Napalm Fire Projectile";
        }
    };






    NapalmProjectile *napalmProjectile = nullptr;

    float throwingVelocity = 1000;

    /*float explosionRadius = 10;
    int numExplosionProjectiles = 100;

    float explosionProjectileVelocityMin = 100;
    float explosionProjectileVelocityMax = 1500;*/

    bool bombThrown = false;

    Napalm() {}
    ~Napalm() {}

    void use(float x, float y, float vx, float vy, float angle) {

        if(!bombThrown) {
            napalmProjectile = new NapalmProjectile();
            napalmProjectile->x = x;
            napalmProjectile->y = y;
            napalmProjectile->vx = throwingVelocity * cos(angle);
            napalmProjectile->vy = throwingVelocity * sin(angle);
            napalmProjectile->gravityHasAnEffect = true;
            //napalmProjectile->characters = characters;
            napalmProjectile->projectileUserCharacter = itemUserCharacter;
            napalmProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(napalmProjectile);
            bombThrown = true;
        }
        else {
            if(napalmProjectile) {
                napalmProjectile->launch(napalmProjectile->x, napalmProjectile->y, napalmProjectile->vx, napalmProjectile->vy, characters, 1, 500, 0, 200);

                //delete clusterBombProjectile;
                napalmProjectile->canBeRemoved = true;
                napalmProjectile = nullptr;
            }
            bombThrown = false;
        }
    }

    string getName() {
        return "Napalm";
    }

    float manaCostPerUse() {
        return 100.0/4.0;
    }
    bool noManaCostPerUse() {
        return bombThrown;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        if(bombThrown) {
            return 0.25;
        }
        else {
            return 0.5;
        }
    }


};

sf::Image Napalm::NapalmProjectile::image;
sf::Texture Napalm::NapalmProjectile::texture;
sf::Sprite Napalm::NapalmProjectile::sprite;

sf::RectangleShape Napalm::NapalmFireProjectile::rect;

sf::SoundBuffer Napalm::NapalmProjectile::explosionSoundBuffer;
















struct Repeller : public Item {

    struct RepellerProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY); // TODO fixme
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Repeller Projectile";
        }
    };

    static sf::SoundBuffer repellerSoundBuffer;
    static bool initialized;

    bool active = false;

    SoundInstance *repellerSoundInstance = nullptr;

    Repeller() {
        if(!initialized) {
            if(!repellerSoundBuffer.loadFromFile("data/audio/vibra6.ogg")) {
                printf("Couldn't open file 'data/audio/vibra6.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~Repeller() {}


    void use(float x, float y, float vx, float vy, float angle) {
        active = true;
    }
    void updateNonActive(Map &map, float dt) {
        if(repellerSoundInstance && repellerSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            repellerSoundInstance->removeWithFadeOut(1.0);
            repellerSoundInstance = nullptr;
        }
    }
    void afterUpdate(bool forceInactive = false) {
        if(forceInactive) {
            active = false;
        }
        if(active && !repellerSoundInstance) {
            repellerSoundInstance = soundWrapper.playSoundBuffer(repellerSoundBuffer, true, 15.0);
        }
        else if(!active && repellerSoundInstance && repellerSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            repellerSoundInstance->removeWithFadeOut(1.0);
            repellerSoundInstance = nullptr;
        }
        active = false;
    }

    void update(Map &map, float dt);


    string getName() {
        return "Repeller";
    }

    float manaCostPerUse() {
        return 0;
    }

    float manaCostPerSecond() {
        return active ? 30 : 0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0;
    }


};
sf::RectangleShape Repeller::RepellerProjectile::rect;

bool Repeller::initialized = false;
sf::SoundBuffer Repeller::repellerSoundBuffer;
















































struct FireProjectile : public Projectile {

    //enum class Type { Basic, FlyingTile };
    enum Direction { Up, Down, Left, Right };
    Direction direction = Up;

    static sf::RectangleShape rect;


    sf::Color color;
    sf::Color color2;
    sf::Color color3;

    //bool isTile = false;
    //Type type = Type::Basic;
    //Tile tile;
    //FlyingTile flyingTile;

    float time = 0;
    float duration = 2, duration2 = 1;

    //float initialVelocityMin = 400, initialVelocityMax = 800;
    /*float risingVelocityMin = 10, risingVelocityMax = 20;
    float airResistanceMin = 0.7, airResistanceMax = 0.85;*/

    float risingVelocityMin = 0, risingVelocityMax = 0;
    float airResistanceMin = 0, airResistanceMax = 0;

    float damage = 1;

    static void prepare() {
        rect = sf::RectangleShape(sf::Vector2f(1, 1));
        rect.setFillColor(sf::Color(255, 255, 255, 255));
        rect.setScale(scaleX, scaleY);


    }


    void update(Map& map, float dt);

    void render(sf::RenderWindow &window, float scaleX, float scaleY) {
        rect.setPosition(x, y);

        if(time < duration) {
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
        }
        else {
            sf::Color c = mix(color2, color3, (time-duration)/duration2);
            rect.setFillColor(c);
        }

        window.draw(rect);
    }


    sf::Color getPixelColor() {
        if(time < duration) {
            return mix(color, color2, time/duration);
        }
        else {
            return mix(color2, color3, (time-duration)/duration2);
        }
    }

    virtual std::string getName() {
        return "Fire Projectile";
    }
};
sf::RectangleShape FireProjectile::rect;














struct FlameThrower : public Item {

    static sf::SoundBuffer flameSoundBuffer;
    static bool initialized;

    //float initialVelocityMin = 600, initialVelocityMax = 1000;
    float initialVelocityMin = 600, initialVelocityMax = 2000;
    float risingVelocityMin = 10, risingVelocityMax = 1000;
    //float airResistance = 0.95;
    //float airResistanceMin = 0.95, airResistanceMax = 0.95;
    float airResistanceMin = 0.90, airResistanceMax = 0.99;

    bool active = false;
    /*float explosionRadius = 10;
    int numExplosionProjectiles = 100;

    float explosionProjectileVelocityMin = 100;
    float explosionProjectileVelocityMax = 1500;*/


    SoundInstance *flameSoundInstance = nullptr;

    FlameThrower() {
        if(!initialized) {
            if(!flameSoundBuffer.loadFromFile("data/audio/fire4b.ogg")) {
                printf("Couldn't open file 'data/audio/fire4b.ogg'!\n");
                return;
            }
            initialized = true;
        }

    }
    ~FlameThrower() {}

    // TODO fixme!
    void use(float x, float y, float vx, float vy, float angle) {
        active = true;
    }

    void update(Map &map, float dt);

    void updateNonActive(Map &map, float dt) {
        if(flameSoundInstance && flameSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            flameSoundInstance->removeWithFadeOut(1.0);
            flameSoundInstance = nullptr;
        }
    }

    void afterUpdate(bool forceInactive = false) {
        if(forceInactive) {
            active = false;
        }

        if(active && !flameSoundInstance) {
            flameSoundInstance = soundWrapper.playSoundBuffer(flameSoundBuffer, true, 100.0);
        }
        else if(!active && flameSoundInstance && flameSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            flameSoundInstance->removeWithFadeOut(1.0);
            flameSoundInstance = nullptr;
        }


        active = false;
    }

    string getName() {
        return "Flame Thrower";
    }

    float manaCostPerUse() {
        return 0;
    }

    float manaCostPerSecond() {
        return active ? 30 : 0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0;
    }


};

bool FlameThrower::initialized = false;
sf::SoundBuffer FlameThrower::flameSoundBuffer;















struct LightningStrikeProjectile : public Projectile {

    static sf::RectangleShape rect;

    static sf::SoundBuffer explosionSoundBuffer;

    sf::Color color, color2;


    float time = 0;
    float duration = 2;

    float currentAngle = 0;
    float initialAngle = 0;

    float angleMin = -0.2*Pi, angleMax = 0.2*Pi;
    float angleChangeMin = -0.1*Pi, angleChangeMax = 0.1*Pi;

    float probabilityOfBranching = 0.005;

    static void prepare() {
        rect = sf::RectangleShape(sf::Vector2f(1, 1));
        rect.setFillColor(sf::Color(255, 255, 255, 255));
        rect.setScale(scaleX, scaleY);

        if(!explosionSoundBuffer.loadFromFile("data/audio/lightning3.ogg")) {
            printf("Couldn't open file 'data/audio/lightning3.ogg'!\n");
            return;
        }
    }

    void update(Map &map, float dt);


    void createLigningTrail(Map &map, LightningStrikeProjectile *lightningStrikeProjectilePrev, int numBranches);


    void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);



    void render(sf::RenderWindow &window, float scaleX, float scaleY) {
        rect.setPosition(x, y);

        sf::Color c = mix(color, color2, time/duration);
        rect.setFillColor(c);

        window.draw(rect);
    }


    sf::Color getPixelColor() {
        return mix(color, color2, time/duration);
    }

    virtual std::string getName() {
        return "Lightning Strike Projectile";
    }
};
sf::RectangleShape LightningStrikeProjectile::rect;
sf::SoundBuffer LightningStrikeProjectile::explosionSoundBuffer;







struct LightningStrike : public Item {

    LightningStrikeProjectile *initialLightningStrikeProjectile = nullptr;

    bool readyToStrike = false;

    LightningStrike() {}
    ~LightningStrike() {}

    void use(float x, float y, float vx, float vy, float angle);

    void update(Map &map, float dt) {
        if(readyToStrike) {
            if(!initialLightningStrikeProjectile) {
                printf("FIX ME: at LightningStrike.update(): !initialLightningStrikeProjectile\n");
            }
            else {
                LightningStrikeProjectile *lightningStrikeProjectile = new LightningStrikeProjectile();
                lightningStrikeProjectile->createLigningTrail(map, initialLightningStrikeProjectile, 1);
                initialLightningStrikeProjectile = nullptr;
                readyToStrike = false;
                soundWrapper.playSoundBuffer(lightningStrikeProjectile->explosionSoundBuffer);
            }
        }
    }


    string getName() {
        return "Lightning Strike";
    }

    float manaCostPerUse() {
        return 100.0/4.0;
    }

    float manaCostPerSecond() {
        return 0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 1;
    }


};




struct Blaster : public Item {

    struct BlasterProjectile : public Projectile {

        static sf::Image imageOneBoltOnly;
        static sf::Texture textureOneBoltOnly;
        static sf::Sprite spriteOneBoltOnly;


        sf::Color color;
        static sf::RectangleShape rect;
        int numBlasterProjectiles = 1;

        bool useOneBoltOnly = false;

        float angleOneBoltOnly = 0;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY);

            if(!imageOneBoltOnly.loadFromFile("data/textures/blasterBolt.png")) {
                printf("Couldn't open file 'data/textures/blasterBolt.png'!\n");
                return;
            }
            textureOneBoltOnly.loadFromImage(imageOneBoltOnly);
            sf::Vector2u size = imageOneBoltOnly.getSize();
            spriteOneBoltOnly.setTexture(textureOneBoltOnly, true);
            spriteOneBoltOnly.setOrigin(size.x/2, size.y/2);
        }

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            /*rect.setPosition(x, y);
            rect.setFillColor(color);
            window.draw(rect);*/

            spriteOneBoltOnly.setPosition(x, y);
            spriteOneBoltOnly.setScale(scaleX, scaleY);
            spriteOneBoltOnly.setRotation(angleOneBoltOnly/Pi*180);
            window.draw(spriteOneBoltOnly);
        }

        sf::Color getPixelColor() {
            return color;
        }

        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool hit = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;

                hit = hit || _update(map, _dt, hit);
            }
        }

        bool _update(Map& map, float dt, bool hit = false);

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);

        bool isRenderSprite() {
            return useOneBoltOnly;
        }

        virtual std::string getName() {
            return "Blaster Projectile";
        }
    };

    static sf::SoundBuffer blasterSoundBuffer;
    static bool initialized;

    float speed = 1500;
    int numBlasterProjectiles = 50;

    bool useOneBoltOnly = false;

    float repeatTime_ = 0.1;

    Blaster() {
        if(!initialized) {
            if(!blasterSoundBuffer.loadFromFile("data/audio/tuff.ogg")) {
                printf("Couldn't open file 'data/audio/tuff.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~Blaster() {}

    void use(float x, float y, float vx, float vy, float angle);


    void update(Map &map, float dt) { }


    string getName() {
        return "Blaster";
    }

    float manaCostPerUse() {
        return 5.0;
    }

    float manaCostPerSecond() {
        return 0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return repeatTime_;
    }


};

sf::Image Blaster::BlasterProjectile::imageOneBoltOnly;
sf::Texture Blaster::BlasterProjectile::textureOneBoltOnly;
sf::Sprite Blaster::BlasterProjectile::spriteOneBoltOnly;

sf::RectangleShape Blaster::BlasterProjectile::rect;

sf::SoundBuffer Blaster::blasterSoundBuffer;
bool Blaster::initialized = false;





















































struct BloodProjectile : public Projectile {

    enum class Type { Basic, FlyingTile };
    enum Direction { Up, Down, Left, Right };
    Direction direction = Up;

    static sf::RectangleShape rect;

    sf::Color color;
    sf::Color color2;

    //bool isTile = false;
    Type type = Type::FlyingTile;
    //Tile tile;
    FlyingTile flyingTile;

    float time = 0;
    float duration = 100;


    static void prepare() {
        rect = sf::RectangleShape(sf::Vector2f(1, 1));
        rect.setFillColor(sf::Color(200, 0, 0, 150));
    }


    void update(Map& map, float dt) {
        if(gravityHasAnEffect) {
            vy += gravity * dt;
        }
        x += vx * dt;
        y += vy * dt;

        time += dt;

        if(time >= duration) {
            canBeRemoved = true;
            return;
        }

        //sf::Vector2u size = image.getSize();
        float mw = map.w * map.scaleX;
        float mh = map.h * map.scaleY;
        float mx = screenW/2 - mw/2;
        float my = screenH/2 - mh/2;

        if(x >= mx + mw) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(mx + mw - 1, screenW);
                int py = map.mapY(y, screenH);;

                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }

            canBeRemoved = true;
        }
        else if(x < mx) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(mx+1, screenW);
                int py = map.mapY(y, screenH);
                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }
            canBeRemoved = true;
        }
        else if(y > my + mh) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(x, screenW);
                int py = map.mapY(my + mh - 1, screenH);

                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }
            canBeRemoved = true;
        }
        else if(y < my) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(x, screenW);
                int py = map.mapY(my+1, screenH);;

                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }
            canBeRemoved = true;
        }
        else {
            int px = map.mapX(x, screenW);
            int py = map.mapY(y, screenH);
            if(px != -1 && py != -1) {
                if(map.tiles[px + py*map.w].type == Map::Tile::Ground || map.tiles[px + py*map.w].type == Map::Tile::Dirt) {
                    if(type == Type::FlyingTile) {
                        int distUp = 0, distDown = 0, distLeft = 0, distRight = 0;

                        while(map.isTileWithin(px, py-distUp) && (map.tiles[px + (py-distUp)*map.w].type == Map::Tile::Ground || map.tiles[px + (py-distUp)*map.w].type == Map::Tile::Dirt)) {
                            distUp++;
                        }
                        while(map.isTileWithin(px, py+distDown) && (map.tiles[px + (py+distDown)*map.w].type == Map::Tile::Ground || map.tiles[px + (py+distDown)*map.w].type == Map::Tile::Dirt)) {
                            distDown++;
                        }
                        while(map.isTileWithin(px-distLeft, py) && (map.tiles[px-distLeft + py*map.w].type == Map::Tile::Ground || map.tiles[px-distLeft + py*map.w].type == Map::Tile::Dirt)) {
                            distLeft++;
                        }
                        while(map.isTileWithin(px+distRight, py) && (map.tiles[px+distRight + py*map.w].type == Map::Tile::Ground || map.tiles[px+distRight + py*map.w].type == Map::Tile::Dirt)) {
                            distRight++;
                        }


                        if(distLeft <= distUp && distLeft <= distDown && distLeft <= distRight) {
                            direction = Direction::Left;
                        }
                        else if(distRight <= distUp && distLeft <= distDown && distRight <= distLeft) {
                            direction = Direction::Right;
                        }
                        else if(distDown <= distUp) {
                            direction = Direction::Down;
                        }
                        else {
                            direction = Direction::Up;
                        }

                        if(direction == Direction::Up) {
                            if(map.isTileWithin(px, py-distUp)) {
                                map.tiles[px + (py-distUp)*map.w] = flyingTile.tile;
                            }

                        }
                        else if(direction == Direction::Down) {
                            if(map.isTileWithin(px, py+distDown)) {
                                map.tiles[px + (py+distDown)*map.w] = flyingTile.tile;
                            }
                        }
                        if(direction == Direction::Left) {
                            if(map.isTileWithin(px-distLeft, py)) {
                                map.tiles[px-distLeft + py*map.w] = flyingTile.tile;
                            }

                        }
                        else if(direction == Direction::Right) {
                            if(map.isTileWithin(px+distRight, py)) {
                                map.tiles[px+distRight + py*map.w] = flyingTile.tile;
                            }
                        }

                    }
                    canBeRemoved = true;
                }
            }
        }

    }

    void render(sf::RenderWindow &window, float scaleX, float scaleY) {
        rect.setPosition(x, y);
        rect.setScale(scaleX, scaleY);
        if(type == Type::FlyingTile) {
            rect.setFillColor(flyingTile.tile.color);
        }
        else {
            //sf::Color c = mix(color, color2, time/duration);
            sf::Color c = color;

            rect.setFillColor(c);
        }
        rect.setFillColor(sf::Color(200, 0, 0, 200));
        window.draw(rect);
    }

    sf::Color getPixelColor() {
        if(type == Type::FlyingTile) {
            return flyingTile.tile.color;
        }
        else {
            return color;
        }
    }

    virtual std::string getName() {
        return "Blood Projectile";
    }
};
sf::RectangleShape BloodProjectile::rect;

















struct LandGrower : public Item {

    struct LandGrowerProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY); // TODO fixme
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Land Grower Projectile";
        }

    };

    static sf::SoundBuffer landGrowerSoundBuffer;
    static bool initialized;

    bool active = false;
    float spawnCounter = 50;
    Map::TilePainter tilePainter;
    string name = "";
    bool rock = true;

    SoundInstance *landGrowerSoundInstance = nullptr;

    bool spawnTiles = false;

    LandGrower(bool rock, bool spawnTiles = true) {
        if(!initialized) {
            if(!landGrowerSoundBuffer.loadFromFile("data/audio/landGrower2.ogg")) {
                printf("Couldn't open file 'data/audio/landGrower2.ogg'!\n");
                return;
            }
            /*if(!landGrowerSoundBuffer.loadFromFile("data/audio/vibra8b.ogg")) {
                printf("Couldn't open file 'data/audio/vibra8b.ogg'!\n");
                return;
            }*/
            initialized = true;
        }

        this->rock = rock;
        if(rock) {
            tilePainter.type = Map::Tile::Type::Ground;
            tilePainter.flammable = false;
            tilePainter.brushSize = 1;
            if(spawnTiles) {
                name = "Rock Tile Spawner";
            }
            else {
                name = "Rock Spawner";
            }
        }
        else {
            tilePainter.type = Map::Tile::Type::Ground;
            tilePainter.flammable = true;
            tilePainter.brushSize = 1;
            if(spawnTiles) {
                name = "Wood Tile Spawner";
            }
            else {
                name = "Wood Spawner";
            }
        }

        this->spawnTiles = spawnTiles;
    }
    ~LandGrower() {}



    void use(float x, float y, float vx, float vy, float angle);/* {
        if(itemUserCharacter->mana >= 100) {
            active = !active;
        }
    }*/

    void update(Map &map, float dt);

    void updateNonActive(Map &map, float dt) {
        if(landGrowerSoundInstance && landGrowerSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            landGrowerSoundInstance->removeWithFadeOut(1.0);
            landGrowerSoundInstance = nullptr;
        }
    }

    void afterUpdate(bool forceInactive = false) {
        if(forceInactive) {
            active = false;
        }

        if(active && !landGrowerSoundInstance) {
            landGrowerSoundInstance = soundWrapper.playSoundBuffer(landGrowerSoundBuffer, true, 10.0);
        }
        else if(!active && landGrowerSoundInstance && landGrowerSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            landGrowerSoundInstance->removeWithFadeOut(1.0);
            landGrowerSoundInstance = nullptr;
        }
        active = false;
    }

    void asdLandGrower(Map &map, int threshold);

    void asdLandGrowerTiles(Map &map, int threshold);

    string getName() {
        return name;
    }

    float manaCostPerUse() {
        return 0;
    }

    float manaCostPerSecond() {
        //return 0;
        return active ? 30 : 0;
    }

    float loadingTimeSeconds() {
        return 180;
    }

    // TODO Handle this!
    float repeatTime() {
        return 0;
    }


};
sf::RectangleShape LandGrower::LandGrowerProjectile::rect;

bool LandGrower::initialized = false;
sf::SoundBuffer LandGrower::landGrowerSoundBuffer;











struct Digger : public Item {

    struct DiggerProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY); // TODO fixme
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Digger Projectile";
        }
    };

    static sf::SoundBuffer diggerSoundBuffer;
    static bool initialized;

    SoundInstance *diggerSoundInstance = nullptr;

    bool active = false;

    float diggingRate = 0.1;
    float diggingTimer = 0;

    Digger() {
        if(!initialized) {
            if(!diggerSoundBuffer.loadFromFile("data/audio/laser8b.ogg")) {
                printf("Couldn't open file 'data/audio/laser8b.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~Digger() {}

    //TODO fixme!
    void use(float x, float y, float vx, float vy, float angle) {
        active = true;
    }
    void updateNonActive(Map &map, float dt) {
        if(diggerSoundInstance && diggerSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            diggerSoundInstance->removeWithFadeOut(1.0);
            diggerSoundInstance = nullptr;
        }
    }
    void afterUpdate(bool forceInactive = false) {
        if(forceInactive) {
            active = false;
        }

        if(active && !diggerSoundInstance) {
            diggerSoundInstance = soundWrapper.playSoundBuffer(diggerSoundBuffer, true, 100.0);
        }
        else if(!active && diggerSoundInstance && diggerSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            diggerSoundInstance->removeWithFadeOut(1.0);
            diggerSoundInstance = nullptr;
        }
        active = false;
    }

    void update(Map &map, float dt);


    string getName() {
        return "Digger";
    }

    float manaCostPerUse() {
        return 0;
    }

    float manaCostPerSecond() {
        //return active ? 30 : 0; // TODO inactivated for debugging purposes
        return 0;
    }

    float loadingTimeSeconds() {
        return 5;
    }

    float repeatTime() {
        return 0;
    }


};
sf::RectangleShape Digger::DiggerProjectile::rect;

bool Digger::initialized = false;
sf::SoundBuffer Digger::diggerSoundBuffer;



















struct JetPack : public Item {

    struct JetPackProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY); // TODO fixme
        }

        void update(Map& map, float dt); /*{
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }*/


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Jet Pack Projectile";
        }
    };


    struct JetPackProjectile2 : public Projectile {
        //static sf::RectangleShape rect;

        sf::Color color1, color2, color3;

        float time = 0;
        float duration1 = 1;
        float duration2 = 1;

        float risingAcceleration = 0;

        static void prepare() {
            /*rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY); // TODO fixme*/
        }

        void update(Map& map, float dt);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            /*rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);*/
        }

        sf::Color getPixelColor() {
            if(time < duration1) {
                return mix(color1, color2, time/duration1);
            }
            else {
                return mix(color2, color3, (time-duration1)/duration2);
            }
        }

        virtual std::string getName() {
            return "Jet Pack Projectile 2";
        }
    };


    static sf::SoundBuffer flameSoundBuffer;
    static bool initialized;

    bool active = false;

    SoundInstance *flameSoundInstance = nullptr;

    JetPack() {
        if(!initialized) {
            if(!flameSoundBuffer.loadFromFile("data/audio/fire3b.ogg")) {
                printf("Couldn't open file 'data/audio/fire3b.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~JetPack() {}

    //TODO fixme!
    void use(float x, float y, float vx, float vy, float angle) {
        active = true;
    }
    void updateNonActive(Map &map, float dt) {
        if(flameSoundInstance && flameSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            flameSoundInstance->removeWithFadeOut(1.0);
            flameSoundInstance = nullptr;
        }
    }
    void afterUpdate(bool forceInactive = false) {
        if(forceInactive) {
            active = false;
        }

        if(active && !flameSoundInstance) {
            flameSoundInstance = soundWrapper.playSoundBuffer(flameSoundBuffer, true, 20.0);
        }
        else if(!active && flameSoundInstance && flameSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            flameSoundInstance->removeWithFadeOut(1.0);
            flameSoundInstance = nullptr;
        }

        active = false;
    }

    void update(Map &map, float dt);


    string getName() {
        return "Jet Pack";
    }

    float manaCostPerUse() {
        return 0;
    }

    float manaCostPerSecond() {
        //return active ? 30 : 0; // TODO inactivated for debugging purposes
        return 0;
    }

    float loadingTimeSeconds() {
        return 5;
    }

    float repeatTime() {
        return 0;
    }


};
sf::RectangleShape JetPack::JetPackProjectile::rect;

bool JetPack::initialized = false;
sf::SoundBuffer JetPack::flameSoundBuffer;



















struct NuclearBomb : public Item {

    struct NuclearWarningProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY); // TODO fixme
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Nuclear Warning Projectile";
        }
    };

    struct NuclearBombProjectile : public Projectile {

        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::Image imageHalo;
        static sf::Texture textureHalo;
        static sf::Sprite spriteHalo;

        static sf::SoundBuffer explosionSoundBuffer;

        bool explode = false;

        static void prepare() {
            if(!image.loadFromFile("data/textures/nuclear.png")) {
                printf("Couldn't open file 'data/textures/nuclear.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!imageHalo.loadFromFile("data/textures/nuclear halo.png")) {
                printf("Couldn't open file 'data/textures/nuclear halo.png'!\n");
                return;
            }
            textureHalo.loadFromImage(imageHalo);
            size = imageHalo.getSize();
            spriteHalo.setTexture(textureHalo, true);
            spriteHalo.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/noiseX.ogg")) {
                printf("Couldn't open file 'data/audio/noiseX.ogg'!\n");
                return;
            }
            /*if(!explosionSoundBuffer.loadFromFile("data/audio/explo4.ogg")) {
                printf("Couldn't open file 'data/audio/explo4.ogg'!\n");
                return;
            }*/
        }

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);


        //void update(Map& map, float dt);

        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool exploded = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;


                exploded = exploded || _update(map, _dt, k, exploded);
                //_update(map, _dt);
            }
        }

        bool _update(Map& map, float dt, int m, bool exploded = false);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            spriteHalo.setColor(sf::Color(255, 0, 0, (int)((sin(timer.totalTime*5.0)*0.5 + 0.5) * 255)));
            spriteHalo.setPosition(x, y);
            spriteHalo.setScale(scaleX, scaleY);
            window.draw(spriteHalo);

            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Nuclear Bomb Projectile";
        }
    };


    float throwingVelocity = 0;
    bool bombThrown = false;
    NuclearBombProjectile *nuclearBombProjectile = nullptr;

    NuclearBomb() {}
    ~NuclearBomb() {}

    void use(float x, float y, float vx, float vy, float angle) {
        if(!bombThrown) {
            nuclearBombProjectile = new NuclearBombProjectile();
            nuclearBombProjectile->x = x;
            nuclearBombProjectile->y = y;
            nuclearBombProjectile->vx = throwingVelocity * cos(angle);
            nuclearBombProjectile->vy = throwingVelocity * sin(angle);
            nuclearBombProjectile->gravityHasAnEffect = true;
            //nuclearBombProjectile->characters = characters;
            nuclearBombProjectile->projectileUserCharacter = itemUserCharacter;
            nuclearBombProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(nuclearBombProjectile);
            bombThrown = true;
        }
        else {
            if(nuclearBombProjectile) {
                nuclearBombProjectile->explode = true;
                //nuclearBombProjectile->canBeRemoved = true;
                nuclearBombProjectile = nullptr;
            }
            bombThrown = false;
        }
    }

    string getName() {
        return "Nuclear Bomb";
    }

    float manaCostPerUse() {
        return 100.0;
    }

    bool noManaCostPerUse() {
        return bombThrown;
    }

    float loadingTimeSeconds() {
        return 90;
    }

    float repeatTime() {
        return 1.0/3.0;
    }

};
sf::RectangleShape NuclearBomb::NuclearWarningProjectile::rect;


sf::Image NuclearBomb::NuclearBombProjectile::image;
sf::Texture NuclearBomb::NuclearBombProjectile::texture;
sf::Sprite NuclearBomb::NuclearBombProjectile::sprite;

sf::Image NuclearBomb::NuclearBombProjectile::imageHalo;
sf::Texture NuclearBomb::NuclearBombProjectile::textureHalo;
sf::Sprite NuclearBomb::NuclearBombProjectile::spriteHalo;

sf::SoundBuffer NuclearBomb::NuclearBombProjectile::explosionSoundBuffer;













struct MissileLauncher : public Item {


    struct MissileSmokeProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY); // TODO fixme
        }

        void update(Map& map, float dt) {


            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }

        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Missile Smoke Projectile";
        }
    };

    struct MissileProjectile : public Projectile {
        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::SoundBuffer explosionSoundBuffer;
        static sf::SoundBuffer flameSoundBuffer;

        //sf::Sound flameSound;
        SoundInstance *flameSoundInstance = nullptr;

        float angle = 0;
        float missileAcceleration = 100;
        //static float missileSize = 0;

        static void prepare() {
            if(!image.loadFromFile("data/textures/missile.png")) {
                printf("Couldn't open file 'data/textures/missile.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/lightning.ogg")) {
                printf("Couldn't open file 'data/audio/lightning.ogg'!\n");
                return;
            }
            if(!flameSoundBuffer.loadFromFile("data/audio/fire3c.ogg")) {
                printf("Couldn't open file 'data/audio/fire3c.ogg'!\n");
                return;
            }
        }

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);


        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool exploded = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                vx += missileAcceleration * _dt * cos(angle);
                vy += missileAcceleration * _dt * sin(angle);

                x += vx * _dt;
                y += vy * _dt;

                exploded = exploded || _update(map, _dt, exploded);
            }



            int numProjectiles = 20;
            float radius = 1*scaleX;

            //float x = itemUserCharacter->x + itemUserCharacter->w*itemUserCharacter->scaleX/2;
            //float y = itemUserCharacter->y + itemUserCharacter->h*itemUserCharacter->scaleY*1.2;

            sf::Vector2u size = image.getSize();
            float missileSize = 0.5 * max(size.x*scaleX, size.y*scaleY);

            for(int i=0; i<numProjectiles; i++) {
                MissileSmokeProjectile *missileSmokeProjectile = new MissileSmokeProjectile();
                //float angle = randf(0.5*Pi-0.1, 0.5*Pi+0.1);
                //float angle = randf(0, 2.0*Pi);
                float v = randf(0, 100);
                //jetPackProjectile->vx = itemUserCharacter->vx + v * cos(angle);
                //jetPackProjectile->vy = itemUserCharacter->vy + v * sin(angle);
                float angle2 = randf(0, 2.0*Pi);
                missileSmokeProjectile->vx = v * cos(angle2);
                missileSmokeProjectile->vy = v * sin(angle2);
                float angle3 = randf(0, 2.0*Pi);
                missileSmokeProjectile->x = x + radius * cos(angle3) + missileSize*cos(angle-Pi);
                missileSmokeProjectile->y = y + radius * sin(angle3) + missileSize*sin(angle-Pi);
                missileSmokeProjectile->gravityHasAnEffect = false;
                int c = randi(50, 200);
                int a = randi(50, 255);
                missileSmokeProjectile->color = sf::Color(c, c, c, a);
                missileSmokeProjectile->color2 = sf::Color(0, 0, 0, 0);
                missileSmokeProjectile->duration = randf(0.5, 1);
                //missileSmokeProjectile->characters = map.characters;
                missileSmokeProjectile->projectileUserCharacter = projectileUserCharacter;
                missileSmokeProjectile->projectileUserVehicle = projectileUserVehicle;
                projectiles.push_back(missileSmokeProjectile);
            }

            for(int i=0; i<numProjectiles; i++) {
                MissileSmokeProjectile *missileSmokeProjectile = new MissileSmokeProjectile();
                float da = randf(-0.5, 0.5);
                float v = randf(10, 200);
                missileSmokeProjectile->vx = v * cos(angle-Pi+da);
                missileSmokeProjectile->vy = v * sin(angle-Pi+da);
                float angle2 = randf(0, 2.0*Pi);
                missileSmokeProjectile->x = x + radius * cos(angle2) + missileSize*cos(angle-Pi);
                missileSmokeProjectile->y = y + radius * sin(angle2) + missileSize*sin(angle-Pi);
                missileSmokeProjectile->gravityHasAnEffect = false;
                int g = randi(0, 255);
                int r = randi(g, 255);
                int a = randi(50, 255);
                missileSmokeProjectile->color = sf::Color(r, g, 0, a);
                missileSmokeProjectile->color2 = sf::Color(200, 0, 0, 0);
                missileSmokeProjectile->duration = randf(0.5, 1);
                //missileSmokeProjectile->characters = map.characters;
                missileSmokeProjectile->projectileUserCharacter = projectileUserCharacter;
                missileSmokeProjectile->projectileUserVehicle = projectileUserVehicle;
                projectiles.push_back(missileSmokeProjectile);
            }

        }

        bool _update(Map& map, float dt, bool exploded = false);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            sprite.setRotation(angle/Pi*180);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Missile Projectile";
        }
    };


    float missileInitialVelocity = 0;
    float missileAcceleration = gravity*1.5;


    MissileLauncher() {}
    ~MissileLauncher() {}

    void use(float x, float y, float vx, float vy, float angle);

    string getName() {
        return "Missile Launcher";
    }

    float manaCostPerUse() {
        return 100.0/4.0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0.5;
    }

};

sf::Image MissileLauncher::MissileProjectile::image;
sf::Texture MissileLauncher::MissileProjectile::texture;
sf::Sprite MissileLauncher::MissileProjectile::sprite;

sf::RectangleShape MissileLauncher::MissileSmokeProjectile::rect;

sf::SoundBuffer MissileLauncher::MissileProjectile::explosionSoundBuffer;
sf::SoundBuffer MissileLauncher::MissileProjectile::flameSoundBuffer;




















struct LaserSight : public Item {

    struct LaserSightProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY); // TODO fixme
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Laser Sight Projectile";
        }
    };

    LaserSight() {}
    ~LaserSight() {}

    bool active = false;

    void use(float x, float y, float vx, float vy, float angle) {
        active = true;
    }
    void afterUpdate(bool forceInactive = false) {
        active = false;
    }

    void update(Map &map, float dt);


    string getName() {
        return "Laser Sight";
    }

    float manaCostPerUse() {
        return 0;
    }

    float manaCostPerSecond() {
        //return active ? 30 : 0;
        return 0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0;
    }


};
sf::RectangleShape LaserSight::LaserSightProjectile::rect;




















struct ReflectorBeam : public Item {

    struct ReflectorBeamProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY);
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Reflector Beam Projectile";
        }
    };
 
    static sf::SoundBuffer beamSoundBuffer;
    static bool initialized;

    SoundInstance *beamSoundInstance = nullptr;


    ReflectorBeam() {
        if(!initialized) {
            if(!beamSoundBuffer.loadFromFile("data/audio/laser9.ogg")) {
                printf("Couldn't open file 'data/audio/laser9.ogg'!\n");
                return;
            }
            initialized = true;
        }

    }
    ~ReflectorBeam() {}

    bool active = false;

    void use(float x, float y, float vx, float vy, float angle) {
        active = true;
    }
    void updateNonActive(Map &map, float dt) {
        if(beamSoundInstance && beamSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            beamSoundInstance->removeWithFadeOut(1.0);
            beamSoundInstance = nullptr;
        }
    }
    void afterUpdate(bool forceInactive = false) {
        if(forceInactive) {
            active = false;
        }

        if(active && !beamSoundInstance) {
            beamSoundInstance = soundWrapper.playSoundBuffer(beamSoundBuffer, true, 50.0);
        }
        else if(!active && beamSoundInstance && beamSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            beamSoundInstance->removeWithFadeOut(1.0);
            beamSoundInstance = nullptr;
        }
        active = false;
    }

    void update(Map &map, float dt);


    string getName() {
        return "Reflector Beam";
    }

    float manaCostPerUse() {
        return 0;
    }

    float manaCostPerSecond() {
        return active ? 30 : 0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0;
    }


};
sf::RectangleShape ReflectorBeam::ReflectorBeamProjectile::rect;

bool ReflectorBeam::initialized = false;
sf::SoundBuffer ReflectorBeam::beamSoundBuffer;

















struct BouncyBomb : public Item {

    struct BouncyBombProjectile : public Projectile {

        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::SoundBuffer explosionSoundBuffer;
        static sf::SoundBuffer collisionSoundBuffer;

        int maxBounces = 200;
        int numBounces = 0;


        static void prepare() {
            if(!image.loadFromFile("data/textures/bouncy bomb.png")) {
                printf("Couldn't open file 'data/textures/bouncy bomb.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/explo5b.ogg")) {
                printf("Couldn't open file 'data/audio/explo5b.ogg'!\n");
                return;
            }

            if(!collisionSoundBuffer.loadFromFile("data/audio/tuff4.ogg")) {
                printf("Couldn't open file 'data/audio/tuff4.ogg'!\n");
                return;
            }
        }

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);


        //void update(Map& map, float dt);

        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool exploded = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;


                exploded = exploded || _update(map, _dt, k, exploded);
                //_update(map, _dt);
            }
        }

        bool _update(Map& map, float dt, int m, bool exploded = false);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Bouncy Bomb Projectile";
        }
    };


    float throwingVelocity = 500;

    BouncyBomb() {}
    ~BouncyBomb() {}

    void use(float x, float y, float vx, float vy, float angle) {
        BouncyBombProjectile *bouncyBombProjectile = new BouncyBombProjectile();
        bouncyBombProjectile->x = x + 30 * cos(angle);
        bouncyBombProjectile->y = y + 30 * sin(angle);
        bouncyBombProjectile->vx = throwingVelocity * cos(angle);
        bouncyBombProjectile->vy = throwingVelocity * sin(angle);
        bouncyBombProjectile->gravityHasAnEffect = true;
        //bouncyBombProjectile->characters = characters;
        bouncyBombProjectile->projectileUserCharacter = itemUserCharacter;
        bouncyBombProjectile->projectileUserVehicle = itemUserVehicle;
        projectiles.push_back(bouncyBombProjectile);
    }

    string getName() {
        return "Bouncy Bomb";
    }

    float manaCostPerUse() {
        return 100.0/10.0;
    }

    /*bool noManaCostPerUse() {
        return bombThrown;
    }*/

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0.1666;
    }

};
//sf::RectangleShape BouncyBomb::BouncyBombProjectile::rect;


sf::Image BouncyBomb::BouncyBombProjectile::image;
sf::Texture BouncyBomb::BouncyBombProjectile::texture;
sf::Sprite BouncyBomb::BouncyBombProjectile::sprite;

sf::SoundBuffer BouncyBomb::BouncyBombProjectile::explosionSoundBuffer;
sf::SoundBuffer BouncyBomb::BouncyBombProjectile::collisionSoundBuffer;















struct Rifle : public Item {

    struct RifleHitProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY);
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Rifle Hit Projectile";
        }
    };

    struct RifleProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;
        float angle = 0;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY);
        }

        void update(Map& map, float dt);


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Rifle Projectile";
        }
    };

    static sf::SoundBuffer rifleSoundBuffer;
    static bool initialized;

    Rifle() {
        if(!initialized) {
            if(!rifleSoundBuffer.loadFromFile("data/audio/rifle.ogg")) {
                printf("Couldn't open file 'data/audio/rifle.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~Rifle() {}

    float velocity = 1000;

    void use(float x, float y, float vx, float vy, float angle) {
        RifleProjectile *rifleProjectile = new RifleProjectile();
        //float m = max(itemUserCharacter->w*itemUserCharacter->scaleX, itemUserCharacter->h*itemUserCharacter->scaleY) * 0.5;

        rifleProjectile->x = x + cos(angle);
        rifleProjectile->y = y + sin(angle);
        rifleProjectile->vx = velocity * cos(angle);
        rifleProjectile->vy = velocity * sin(angle);
        rifleProjectile->angle = angle;
        rifleProjectile->gravityHasAnEffect = false;
        rifleProjectile->repellerHasAnEffect = false;
        //rifleProjectile->characters = characters;
        rifleProjectile->projectileUserCharacter = itemUserCharacter;
        rifleProjectile->projectileUserVehicle = itemUserVehicle;
        projectiles.push_back(rifleProjectile);

        soundWrapper.playSoundBuffer(rifleSoundBuffer);
    }
    void afterUpdate(bool forceInactive = false) {

    }

    void update(Map &map, float dt) {}


    string getName() {
        return "Rifle";
    }

    float manaCostPerUse() {
        return 100.0/3.0;
    }

    float manaCostPerSecond() {
        //return active ? 30 : 0;
        return 0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0.5;
    }


};
sf::RectangleShape Rifle::RifleProjectile::rect;
sf::RectangleShape Rifle::RifleHitProjectile::rect;

sf::SoundBuffer Rifle::rifleSoundBuffer;
bool Rifle::initialized = false;






















struct Shotgun : public Item {

    struct ShotgunProjectile : public Projectile {

        sf::Color color;
        static sf::RectangleShape rect;
        int numShotgunProjectiles = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY);
        }

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);

            rect.setFillColor(color);

            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return color;
        }

        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool hit = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;

                hit = hit || _update(map, _dt, hit);
            }
        }

        bool _update(Map& map, float dt, bool hit = false);

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);

        virtual std::string getName() {
            return "Shotgun Projectile";
        }
    };

    static sf::SoundBuffer shotgunSoundBuffer;
    static bool initialized;

    float speedMin = 1000, speedMax = 2500;
    float deltaAngle = Pi*0.1;
    int numShotgunProjectiles = 150;

    Shotgun() {
        if(!initialized) {
            if(!shotgunSoundBuffer.loadFromFile("data/audio/noise8.ogg")) {
                printf("Couldn't open file 'data/audio/noise8.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~Shotgun() {}

    void use(float x, float y, float vx, float vy, float angle);


    void update(Map &map, float dt) { }


    string getName() {
        return "Shotgun";
    }

    float manaCostPerUse() {
        return 25;
    }

    float manaCostPerSecond() {
        return 0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0.25;
    }


};
sf::RectangleShape Shotgun::ShotgunProjectile::rect;

sf::SoundBuffer Shotgun::shotgunSoundBuffer;
bool Shotgun::initialized = false;
















struct DoomsDay : public Item {

    struct DoomsDaySmokeProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY); // TODO fixme
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Doom's Day Smoke Projectile";
        }
    };

    struct DoomsDayProjectile : public Projectile {

        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::SoundBuffer explosionSoundBuffer;
        static sf::SoundBuffer smokeSoundBuffer;

        SoundInstance *smokeSoundInstance = nullptr;

        int maxBounces = 30;
        int numBounces = 0;

        float counter = 0;


        static void prepare() {
            if(!image.loadFromFile("data/textures/doomsday.png")) {
                printf("Couldn't open file 'data/textures/doomsday.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/explo12b.ogg")) {
                printf("Couldn't open file 'data/audio/explo12b.ogg'!\n");
                return;
            }

            if(!smokeSoundBuffer.loadFromFile("data/audio/doomsday.ogg")) {
                printf("Couldn't open file 'data/audio/doomsday.ogg'!\n");
                return;
            }
            /*if(!smokeSoundBuffer.loadFromFile("data/audio/vibra17.ogg")) {
                printf("Couldn't open file 'data/audio/vibra17.ogg'!\n");
                return;
            }*/
        }

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);


        //void update(Map& map, float dt);

        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool exploded = false;
            bool characterHit = false;
            bool vehicleHit = false;

            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;


                exploded = exploded || _update(map, _dt, k, exploded, characterHit, vehicleHit);
                //_update(map, _dt);
            }
        }

        //bool _update(Map& map, float dt, int m, bool exploded, bool &characterHit);

        bool _update(Map& map, float dt, int m, bool exploded, bool &characterHit, bool &vehicleHit);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Doom's Day Projectile";
        }
    };


    float throwingVelocity = 1000;



    DoomsDay() {}
    ~DoomsDay() {}

    void use(float x, float y, float vx, float vy, float angle) {
        DoomsDayProjectile *doomsDayProjectile = new DoomsDayProjectile();
        doomsDayProjectile->x = x;
        doomsDayProjectile->y = y;
        doomsDayProjectile->vx = throwingVelocity * cos(angle);
        doomsDayProjectile->vy = throwingVelocity * sin(angle);
        doomsDayProjectile->gravityHasAnEffect = true;
        //doomsDayProjectile->characters = characters;
        doomsDayProjectile->projectileUserCharacter = itemUserCharacter;
        doomsDayProjectile->projectileUserVehicle = itemUserVehicle;
        doomsDayProjectile->counter = 0;
        projectiles.push_back(doomsDayProjectile);

        doomsDayProjectile->smokeSoundInstance = soundWrapper.playSoundBuffer(doomsDayProjectile->smokeSoundBuffer, true, 20);
    }

    string getName() {
        return "Doom's Day";
    }

    float manaCostPerUse() {
        return 100.0;
    }

    /*bool noManaCostPerUse() {
        return bombThrown;
    }*/

    float loadingTimeSeconds() {
        return 180;
    }

    float repeatTime() {
        return 0.25;
    }

};
//sf::RectangleShape BouncyBomb::BouncyBombProjectile::rect;


sf::Image DoomsDay::DoomsDayProjectile::image;
sf::Texture DoomsDay::DoomsDayProjectile::texture;
sf::Sprite DoomsDay::DoomsDayProjectile::sprite;

sf::SoundBuffer DoomsDay::DoomsDayProjectile::explosionSoundBuffer;
sf::SoundBuffer DoomsDay::DoomsDayProjectile::smokeSoundBuffer;

sf::RectangleShape DoomsDay::DoomsDaySmokeProjectile::rect;






















struct Bolter : public Item {


    struct BolterSmokeProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY); // TODO fixme
        }

        void update(Map& map, float dt) {


            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }

        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
           return "Bolter Smoke Projectile";
        }

    };

    struct BoltProjectile : public Projectile {
        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::SoundBuffer explosionSoundBuffer;
        static sf::SoundBuffer flameSoundBuffer;

        //sf::Sound flameSound;
        SoundInstance *flameSoundInstance = nullptr;

        float angle = 0;
        float boltAcceleration = 100;
        float explosionRadius = 3;

        static void prepare() {
            if(!image.loadFromFile("data/textures/bolt.png")) {
                printf("Couldn't open file 'data/textures/bolt.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/bolterHit.ogg")) {
                printf("Couldn't open file 'data/audio/bolterHit.ogg'!\n");
                return;
            }
            if(!flameSoundBuffer.loadFromFile("data/audio/bolterPropel.ogg")) {
                printf("Couldn't open file 'data/audio/bolterPropel.ogg'!\n");
                return;
            }
        }

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);


        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool exploded = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                vx += boltAcceleration * _dt * cos(angle);
                vy += boltAcceleration * _dt * sin(angle);

                x += vx * _dt;
                y += vy * _dt;

                exploded = exploded || _update(map, _dt, exploded);
            }



            int numProjectiles = 10;
            float radius = 1*scaleX;

            //float x = itemUserCharacter->x + itemUserCharacter->w*itemUserCharacter->scaleX/2;
            //float y = itemUserCharacter->y + itemUserCharacter->h*itemUserCharacter->scaleY*1.2;

            sf::Vector2u size = image.getSize();
            float boltSize = 0.5 * max(size.x*scaleX, size.y*scaleY);

            for(int i=0; i<numProjectiles; i++) {
                BolterSmokeProjectile *bolterSmokeProjectile = new BolterSmokeProjectile();
                //float angle = randf(0.5*Pi-0.1, 0.5*Pi+0.1);
                //float angle = randf(0, 2.0*Pi);
                float v = randf(0, 100);
                //jetPackProjectile->vx = itemUserCharacter->vx + v * cos(angle);
                //jetPackProjectile->vy = itemUserCharacter->vy + v * sin(angle);
                float angle2 = randf(0, 2.0*Pi);
                bolterSmokeProjectile->vx = v * cos(angle2);
                bolterSmokeProjectile->vy = v * sin(angle2);
                float angle3 = randf(0, 2.0*Pi);
                bolterSmokeProjectile->x = x + radius * cos(angle3) + boltSize*cos(angle-Pi);
                bolterSmokeProjectile->y = y + radius * sin(angle3) + boltSize*sin(angle-Pi);
                bolterSmokeProjectile->gravityHasAnEffect = false;
                int c = randi(50, 200);
                int a = randi(50, 255);
                bolterSmokeProjectile->color = sf::Color(c, c, c, a);
                bolterSmokeProjectile->color2 = sf::Color(0, 0, 0, 0);
                bolterSmokeProjectile->duration = randf(0.15, 0.3);
                //bolterSmokeProjectile->characters = map.characters;
                bolterSmokeProjectile->projectileUserCharacter = projectileUserCharacter;
                bolterSmokeProjectile->projectileUserVehicle = projectileUserVehicle;
                projectiles.push_back(bolterSmokeProjectile);
            }

            for(int i=0; i<numProjectiles; i++) {
                BolterSmokeProjectile *bolterSmokeProjectile = new BolterSmokeProjectile();
                float da = randf(-0.5, 0.5);
                float v = randf(10, 200);
                bolterSmokeProjectile->vx = v * cos(angle-Pi+da);
                bolterSmokeProjectile->vy = v * sin(angle-Pi+da);
                float angle2 = randf(0, 2.0*Pi);
                bolterSmokeProjectile->x = x + radius * cos(angle2) + boltSize*cos(angle-Pi);
                bolterSmokeProjectile->y = y + radius * sin(angle2) + boltSize*sin(angle-Pi);
                bolterSmokeProjectile->gravityHasAnEffect = false;
                int g = randi(0, 255);
                int r = randi(g, 255);
                int a = randi(50, 255);
                bolterSmokeProjectile->color = sf::Color(r, g, 0, a);
                bolterSmokeProjectile->color2 = sf::Color(200, 0, 0, 0);
                bolterSmokeProjectile->duration = randf(0.15, 0.3);
                //bolterSmokeProjectile->characters = map.characters;
                bolterSmokeProjectile->projectileUserCharacter = projectileUserCharacter;
                bolterSmokeProjectile->projectileUserVehicle = projectileUserVehicle;
                projectiles.push_back(bolterSmokeProjectile);
            }

        }

        bool _update(Map& map, float dt, bool exploded = false);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            sprite.setRotation(angle/Pi*180);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Bolt Projectile";
        }
    };


    static sf::SoundBuffer bolterLaunchSoundBuffer;
    static bool initialized;

    float boltInitialVelocity = 200;
    float boltAcceleration = gravity;
    int numBolts = 1;
    float explosionRadius = 10;
    std::string name = "Bolter";
    float repeatTime_ = 0.1;
    float manaCostPerUse_ = 100.0 / 32.0;
    float loadingTimeSeconds_ = 30;

    Bolter() {
        if(!initialized) {
            if(!bolterLaunchSoundBuffer.loadFromFile("data/audio/bolterLaunch.ogg")) {
                printf("Couldn't open file 'data/audio/bolterLaunch.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~Bolter() {}

    void use(float x, float y, float vx, float vy, float angle);

    string getName() {
        return name;
    }

    float manaCostPerUse() {
        return manaCostPerUse_;
    }

    float loadingTimeSeconds() {
        return loadingTimeSeconds_;
    }

    float repeatTime() {
        return repeatTime_;
    }

};

sf::Image Bolter::BoltProjectile::image;
sf::Texture Bolter::BoltProjectile::texture;
sf::Sprite Bolter::BoltProjectile::sprite;

sf::RectangleShape Bolter::BolterSmokeProjectile::rect;

sf::SoundBuffer Bolter::bolterLaunchSoundBuffer;
bool Bolter::initialized = false;
sf::SoundBuffer Bolter::BoltProjectile::explosionSoundBuffer;
sf::SoundBuffer Bolter::BoltProjectile::flameSoundBuffer;


















struct Railgun : public Item {

    struct RailgunSmokeProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY);
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Railgun Smoke Projectile";
        }
    };

    struct RailgunProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;
        float angle = 0;

        static sf::SoundBuffer explosionSoundBuffer;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY);

            if(!explosionSoundBuffer.loadFromFile("data/audio/railgunHit.ogg")) {
                printf("Couldn't open file 'data/audio/railgunHit.ogg'!\n");
                return;
            }
        }

        void update(Map& map, float dt);

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Railgun Projectile";
        }
    };

    static sf::SoundBuffer railgunSoundBuffer;
    static bool initialized;

    Railgun() {
        if(!initialized) {
            if(!railgunSoundBuffer.loadFromFile("data/audio/railgunLaunch3.ogg")) {
                printf("Couldn't open file 'data/audio/railgunLaunch3.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~Railgun() {}

    float velocity = 1000;

    void use(float x, float y, float vx, float vy, float angle) {
        RailgunProjectile *railgunProjectile = new RailgunProjectile();

        railgunProjectile->x = x + cos(angle);
        railgunProjectile->y = y + sin(angle);
        railgunProjectile->vx = velocity * cos(angle);
        railgunProjectile->vy = velocity * sin(angle);
        railgunProjectile->angle = angle;
        railgunProjectile->gravityHasAnEffect = false;
        railgunProjectile->repellerHasAnEffect = false;
        //railgunProjectile->characters = characters;
        railgunProjectile->projectileUserCharacter = itemUserCharacter;
        railgunProjectile->projectileUserVehicle = itemUserVehicle;
        projectiles.push_back(railgunProjectile);

        soundWrapper.playSoundBuffer(railgunSoundBuffer);
    }
    void afterUpdate(bool forceInactive = false) {

    }

    void update(Map &map, float dt) {}


    string getName() {
        return "Railgun";
    }

    float manaCostPerUse() {
        return 100.0;
    }

    float manaCostPerSecond() {
        //return active ? 30 : 0;
        return 0;
    }

    float loadingTimeSeconds() {
        return 30;
    }

    float repeatTime() {
        return 0.5;
    }


};
sf::RectangleShape Railgun::RailgunSmokeProjectile::rect;

sf::SoundBuffer Railgun::RailgunProjectile::explosionSoundBuffer;
sf::RectangleShape Railgun::RailgunProjectile::rect;

sf::SoundBuffer Railgun::railgunSoundBuffer;
bool Railgun::initialized = false;
























struct DirtCannon : public Item {

    struct DirtCannonWarningProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY);
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Dirt Cannon Warning Projectile";
        }
    };

    /*struct RailgunProjectile : public Projectile {
        static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;
        float angle = 0;

        static sf::SoundBuffer explosionSoundBuffer;

        static void prepare() {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY);

            if(!explosionSoundBuffer.loadFromFile("data/audio/railgunHit.ogg")) {
                printf("Couldn't open file 'data/audio/railgunHit.ogg'!\n");
                return;
            }
        }

        void update(Map& map, float dt);

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            rect.setPosition(x, y);
            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);
            window.draw(rect);
        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

    };*/

    static sf::SoundBuffer dirtCannonSoundBuffer;
    static bool initialized;

    int numWarningProjectiles = 500;
    float deltaAngle = 0.1 * Pi;
    float range = 300;

    float damage = 0.5;

    float explosionProjectileVelocityMin = 1000;
    float explosionProjectileVelocityMax = 2000;

    bool launch = false;

    std::string name = "Melter Cannon";

    DirtCannon() {
        if(!initialized) {
            if(!dirtCannonSoundBuffer.loadFromFile("data/audio/melterCannon.ogg")) {
                printf("Couldn't open file 'data/audio/melterCannon.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~DirtCannon() {}

    //float velocity = 1000;

    void use(float x, float y, float vx, float vy, float angle); 

    void afterUpdate(bool forceInactive = false) {}

    void update(Map &map, float dt);


    string getName() {
        return name;
    }

    float manaCostPerUse() {
        return 100.0;
    }

    float manaCostPerSecond() {
        return 0;
    }

    float loadingTimeSeconds() {
        return 5;
    }

    float repeatTime() {
        return 0.5;
    }


};
sf::RectangleShape DirtCannon::DirtCannonWarningProjectile::rect;

//sf::SoundBuffer Railgun::RailgunProjectile::explosionSoundBuffer;
//sf::RectangleShape Railgun::RailgunProjectile::rect;

sf::SoundBuffer DirtCannon::dirtCannonSoundBuffer;
bool DirtCannon::initialized = false;




















struct Earthquake : public Item {

    static sf::SoundBuffer earthquakeSoundBuffer;
    static bool initialized;

    bool active = false;

    Earthquake() {
        if(!initialized) {
            if(!earthquakeSoundBuffer.loadFromFile("data/audio/earthquake.ogg")) {
                printf("Couldn't open file 'data/audio/earthquake.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~Earthquake() {}


    void use(float x, float y, float vx, float vy, float angle) {
        active = true;
    }

    void updateNonActive(Map &map, float dt) {}
    void afterUpdate(bool forceInactive = false) {}

    void update(Map &map, float dt) {
        if(active) {
            map.startEarthquake(Map::EarthquakeType::earthquakeActual);
            soundWrapper.playSoundBuffer(earthquakeSoundBuffer, false, 100);
            active = false;
        }
    }


    string getName() {
        return "Earthquake";
    }

    float manaCostPerUse() {
        return 100;
    }

    float manaCostPerSecond() {
        return 0;
    }

    float loadingTimeSeconds() {
        return 10 * 60;
    }

    float repeatTime() {
        return 1;
    }


};
//sf::RectangleShape Repeller::RepellerProjectile::rect;

bool Earthquake::initialized = false;
sf::SoundBuffer Earthquake::earthquakeSoundBuffer;















struct HeavyFlamer : public Item {

    static sf::SoundBuffer flameSoundBuffer;
    static bool initialized;

    //float initialVelocityMin = 600, initialVelocityMax = 2000;
    //float risingVelocityMin = 10, risingVelocityMax = 1000;
    //float airResistanceMin = 0.90, airResistanceMax = 0.99;

    /*float initialVelocityMin = 1200, initialVelocityMax = 3000;
    float risingVelocityMin = 10, risingVelocityMax = 1000;
    float airResistanceMin = 0.95, airResistanceMax = 0.99;*/

    float initialVelocityMin = 1000, initialVelocityMax = 2000;
    float risingVelocityMin = 10, risingVelocityMax = 1000;
    float airResistanceMin = 0.99, airResistanceMax = 0.999;

    bool active = false;

    SoundInstance *flameSoundInstance = nullptr;

    HeavyFlamer() {
        if(!initialized) {
            if(!flameSoundBuffer.loadFromFile("data/audio/heavyFlamer.ogg")) {
                printf("Couldn't open file 'data/audio/heavyFlamer.ogg'!\n");
                return;
            }
            initialized = true;
        }

    }
    ~HeavyFlamer() {}

    // TODO fixme!
    void use(float x, float y, float vx, float vy, float angle) {
        active = true;
    }

    void update(Map &map, float dt);

    void updateNonActive(Map &map, float dt) {
        if(flameSoundInstance && flameSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            flameSoundInstance->removeWithFadeOut(1.0);
            flameSoundInstance = nullptr;
        }
    }

    void afterUpdate(bool forceInactive = false) {
        if(forceInactive) {
            active = false;
        }

        if(active && !flameSoundInstance) {
            flameSoundInstance = soundWrapper.playSoundBuffer(flameSoundBuffer, true, 100.0);
        }
        else if(!active && flameSoundInstance && flameSoundInstance->sound.getStatus() == sf::SoundSource::Status::Playing) {
            flameSoundInstance->removeWithFadeOut(1.0);
            flameSoundInstance = nullptr;
        }


        active = false;
    }

    string getName() {
        return "Heavy Flamer";
    }

    float manaCostPerUse() {
        return 0;
    }

    float manaCostPerSecond() {
        return active ? 25 : 0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0;
    }


};

bool HeavyFlamer::initialized = false;
sf::SoundBuffer HeavyFlamer::flameSoundBuffer;



















struct LaserCannon : public Item {


    struct LaserBoltProjectile : public Projectile {
        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::SoundBuffer explosionSoundBuffer;
        //static sf::SoundBuffer flameSoundBuffer;

        //sf::Sound flameSound;
        //SoundInstance *flameSoundInstance = nullptr;

        float angle = 0;
        float laserBoltExplosionRadius = 100;
        float laserBoltDamage = 100.0/3.0;


        static void prepare() {
            if(!image.loadFromFile("data/textures/laserCannonBolt.png")) {
                printf("Couldn't open file 'data/textures/laserCannonBolt.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/laserCannonExplosion.ogg")) {
                printf("Couldn't open file 'data/audio/laserCannonExplosion.ogg'!\n");
                return;
            }
            /*if(!flameSoundBuffer.loadFromFile("data/audio/fire3c.ogg")) {
                printf("Couldn't open file 'data/audio/fire3c.ogg'!\n");
                return;
            }*/
        }

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);


        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool exploded = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }

                x += vx * _dt;
                y += vy * _dt;

                exploded = exploded || _update(map, _dt, exploded);
            }

        }

        bool _update(Map& map, float dt, bool exploded = false);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            sprite.setRotation(angle/Pi*180);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Laser Bolt Projectile";
        }
    };

    static sf::SoundBuffer laserCannonSoundBuffer;
    static bool initialized;

    //float missileInitialVelocity = 0;
    //float missileAcceleration = gravity*1.5;

    float laserBoltVelocity = 2000;
    float laserBoltExplosionRadius = 100;


    LaserCannon() {
        if(!initialized) {
            if(!laserCannonSoundBuffer.loadFromFile("data/audio/laserCannon.ogg")) {
                printf("Couldn't open file 'data/audio/laserCannon.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~LaserCannon() {}

    void use(float x, float y, float vx, float vy, float angle);

    string getName() {
        return "Laser Cannon";
    }

    float manaCostPerUse() {
        return 100.0/20.0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0.2;
    }

};

sf::Image LaserCannon::LaserBoltProjectile::image;
sf::Texture LaserCannon::LaserBoltProjectile::texture;
sf::Sprite LaserCannon::LaserBoltProjectile::sprite;

//sf::RectangleShape MissileLauncher::MissileSmokeProjectile::rect;

sf::SoundBuffer LaserCannon::LaserBoltProjectile::explosionSoundBuffer;
//sf::SoundBuffer MissileLauncher::MissileProjectile::flameSoundBuffer;

sf::SoundBuffer LaserCannon::laserCannonSoundBuffer;
bool LaserCannon::initialized = false;






















struct ClusterMortar : public Item {

    struct ClusterProjectile : public Projectile {
        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::SoundBuffer explosionSoundBuffer;

        float explosionRadius = 75;

        float damage = 20;

        static void prepare() {
            if(!image.loadFromFile("data/textures/cluster.png")) {
                printf("Couldn't open file 'data/textures/cluster.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/clusterMortarExplosion.ogg")) {
                printf("Couldn't open file 'data/audio/clusterMortarExplosion.ogg'!\n");
                return;
            }
        }

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);


        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool exploded = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;

                exploded = exploded || _update(map, _dt, exploded);
            }
        }

        bool _update(Map& map, float dt, bool exploded = false);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Cluster Mortar Projectile";
        }
    };

    static sf::SoundBuffer clusterMortarSoundBuffer;
    static bool initialized;

    static constexpr float throwingVelocity = 1600;

    int numClusters = 10;


    ClusterMortar() {
        if(!initialized) {
            if(!clusterMortarSoundBuffer.loadFromFile("data/audio/clusterMortarLaunch.ogg")) {
                printf("Couldn't open file 'data/audio/clusterMortarLaunch.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~ClusterMortar() {}

    void use(float x, float y, float vx, float vy, float angle);

    string getName() {
        return "Cluster Mortar";
    }

    float manaCostPerUse() {
        return 100.0/6.0;
    }

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 0.5;
    }

};

sf::Image ClusterMortar::ClusterProjectile::image;
sf::Texture ClusterMortar::ClusterProjectile::texture;
sf::Sprite ClusterMortar::ClusterProjectile::sprite;

sf::SoundBuffer ClusterMortar::ClusterProjectile::explosionSoundBuffer;

sf::SoundBuffer ClusterMortar::clusterMortarSoundBuffer;
bool ClusterMortar::initialized = false;













struct SpawnVehicle : public Item {

    static sf::SoundBuffer spawnVehicleSoundBuffer;
    static bool initialized;

    bool active = false;

    enum class VehicleType { Walker };
    VehicleType vehicleType = VehicleType::Walker;

    SpawnVehicle() {
        if(!initialized) {
            if(!spawnVehicleSoundBuffer.loadFromFile("data/audio/spawnWalker.ogg")) {
                printf("Couldn't open file 'data/audio/spawnWalker.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~SpawnVehicle() {}


    void use(float x, float y, float vx, float vy, float angle) {
        active = true;
    }

    void updateNonActive(Map &map, float dt) {}
    void afterUpdate(bool forceInactive = false) {}

    void update(Map &map, float dt);


    string getName() {
        if(vehicleType == VehicleType::Walker) {
            return "Spawn a Walker";
        }
        else {
            return "<Spawn a vehicle>";
        }
    }

    float manaCostPerUse() {
        return 100;
    }

    float manaCostPerSecond() {
        return 0;
    }

    float loadingTimeSeconds() {
        return 5 * 60;
    }

    float repeatTime() {
        return 1;
    }


};
//sf::RectangleShape Repeller::RepellerProjectile::rect;

bool SpawnVehicle::initialized = false;
sf::SoundBuffer SpawnVehicle::spawnVehicleSoundBuffer;

















struct SpawnRobotGroup : public Item {

    static sf::SoundBuffer spawnRobotGroupSoundBuffer;
    static bool initialized;

    bool active = false;

    int numRobotsToSpawn = 1;

    SpawnRobotGroup() {
        if(!initialized) {
            if(!spawnRobotGroupSoundBuffer.loadFromFile("data/audio/spawnRobots.ogg")) {
                printf("Couldn't open file 'data/audio/spawnRobots.ogg'!\n");
                return;
            }
            initialized = true;
        }
    }
    ~SpawnRobotGroup() {}


    void use(float x, float y, float vx, float vy, float angle) {
        active = true;
    }

    void updateNonActive(Map &map, float dt) {}
    void afterUpdate(bool forceInactive = false) {}

    void update(Map &map, float dt);


    string getName() {
        return "Spawn Robots";
    }

    float manaCostPerUse() {
        return 10;
    }

    float manaCostPerSecond() {
        return 0;
    }

    float loadingTimeSeconds() {
        return 5 * 60;
    }

    float repeatTime() {
        return 1;
    }


};
//sf::RectangleShape Repeller::RepellerProjectile::rect;

bool SpawnRobotGroup::initialized = false;
sf::SoundBuffer SpawnRobotGroup::spawnRobotGroupSoundBuffer;
















struct FireBall : public Item {

    struct FireBallFireProjectile : public Projectile {
        //static sf::RectangleShape rect;
        sf::Color color1;
        sf::Color color2;
        sf::Color color3;

        float time = 0;
        float duration1 = 3;
        float duration2 = 3;

        float risingAcceleration = 100;

        /*static void prepare() {

            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(scaleX, scaleY);
        }*/

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }

            vy -= dt * risingAcceleration;

            vx = vx * 0.9;
            vy = vy * 0.9;

            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration1 + duration2) {
                canBeRemoved = true;
                return;
            }
        }

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            /*rect.setPosition(x, y);
            //rect.setScale(scaleX, scaleY);

            sf::Color c = mix(color, color2, time/duration);
            rect.setFillColor(c);

            window.draw(rect);

            printf("Rendering smoke???\n");*/
        }

        sf::Color getPixelColor() {
            if(time < duration1) {
                return mix(color1, color2, time/duration1);
            }
            else {
                return mix(color2, color3, (time-duration1)/duration2);
            }
        }
        virtual std::string getName() {
            return "Fire Ball Fire Projectile";
        }
    };


    struct FireBallProjectile : public Projectile {

        static sf::Image image;
        static sf::Texture texture;
        static sf::Sprite sprite;

        static sf::SoundBuffer explosionSoundBuffer;
        static sf::SoundBuffer collisionSoundBuffer;

        int maxBounces = 5;
        int numBounces = 0;


        static void prepare() {
            if(!image.loadFromFile("data/textures/fireball.png")) {
                printf("Couldn't open file 'data/textures/fireball.png'!\n");
                return;
            }
            texture.loadFromImage(image);
            sf::Vector2u size = image.getSize();
            sprite.setTexture(texture, true);
            sprite.setOrigin(size.x/2, size.y/2);

            if(!explosionSoundBuffer.loadFromFile("data/audio/fireballExplosion.ogg")) {
                printf("Couldn't open file 'data/audio/fireballExplosion.ogg'!\n");
                return;
            }

            if(!collisionSoundBuffer.loadFromFile("data/audio/fireballBounce.ogg")) {
                printf("Couldn't open file 'data/audio/fireballBounce.ogg'!\n");
                return;
            }
        }

        void createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax);


        //void update(Map& map, float dt);

        void update(Map& map, float dt) {
            float k = round(max(1+fabs(vx) * dt, 1+fabs(vy) * dt));
            k = min(k, 100);
            if(k < 1) k = 1;
            float _dt = dt/k;

            bool exploded = false;
            for(int i=0; i<k; i++) {
                if(gravityHasAnEffect) {
                    vy += gravity * _dt;
                }
                x += vx * _dt;
                y += vy * _dt;


                exploded = exploded || _update(map, _dt, k, exploded);
                //_update(map, _dt);
            }
        }

        bool _update(Map& map, float dt, int m, bool exploded = false);

        void render(sf::RenderWindow &window, float scaleX, float scaleY) {
            sprite.setPosition(x, y);
            sprite.setScale(scaleX, scaleY);
            window.draw(sprite);
        }

        bool isRenderSprite() {
            return true;
        }

        virtual std::string getName() {
            return "Fire Ball Projectile";
        }
    };

    int numFireBalls = 5;
    float throwingVelocityMin = 600;
    float throwingVelocityMax = 1000;
    float deltaAngle = 0.1 * Pi;

    FireBall() {}
    ~FireBall() {}

    void use(float x, float y, float vx, float vy, float angle);

    string getName() {
        return "Fire Balls";
    }

    float manaCostPerUse() {
        return 100.0/3.0;
    }

    /*bool noManaCostPerUse() {
        return bombThrown;
    }*/

    float loadingTimeSeconds() {
        return 10;
    }

    float repeatTime() {
        return 1;
    }

};


sf::Image FireBall::FireBallProjectile::image;
sf::Texture FireBall::FireBallProjectile::texture;
sf::Sprite FireBall::FireBallProjectile::sprite;

sf::SoundBuffer FireBall::FireBallProjectile::explosionSoundBuffer;
sf::SoundBuffer FireBall::FireBallProjectile::collisionSoundBuffer;



















struct Vehicle {

    
    Character *driverCharacter = nullptr;

    float x = 0, y = 0;
    float vx = 0, vy = 0;
    float speed = 75;
    //float m = 1;
    int w = 1, h = 1;
    bool readyToJump = false;
    int scaleX = 1;
    int scaleY = 1;

    float centerX = 0, centerY = 0;

    float aimAngle = 0;
    float globalAngle = 0;

    float maxHp = 100;
    float hp = 100;

    float maxMana = 100;
    //float mana = 100;


    enum Direction { Right, Left };
    Direction direction = Right;

    sf::Image vehicleImageStanding[2];
    sf::Texture vehicleTextureStanding[2];
    sf::Sprite vehicleSpriteStanding[2];

    std::vector<sf::Image> vehicleImageRunning[2];
    std::vector<sf::Texture> vehicleTextureRunning[2];
    std::vector<sf::Sprite> vehicleSpriteRunning[2];


    sf::Image crosshairImage;
    sf::Texture crosshairTexture;
    sf::Sprite crosshairSprite;

    vector<Item*> items;
    int activeItem = 0;

    vector<Item*> itemsSecondary;
    int activeItemSecondary = 0;

    Map *map = nullptr;

    int numRunningAnimationFrames = 8;
    int runningAnimationCounter = 0;
    long runningAnimationCounterX = 0;


    bool doubleClickedItemChange = false;
    float doubleClickedItemChangeTimer = -1;
    float doubleClickDurationSeconds = 0.25;
    int doubleClickedItemChangeActive = 0;


    bool canBeRemoved = false;

    int spritePositionDeltaY = 0;

    
    std::vector<const sf::Uint8 *> pixelsVector;
    int pixelsVectorIndex = 0;

    bool running = false;

    int team = -1;

    virtual ~Vehicle() {}
    virtual void setup(Map *map, const string &filenameBase, int team = -1) = 0;
    virtual void update(float dt, int screenW, int screenH, Map &map) = 0;
    virtual void render(sf::RenderWindow &window) = 0;
    virtual void takeDamage(float amount) = 0;
    virtual void enterThisVehicle(const std::vector<Character*> &characters) = 0;
    virtual void exitThisVehicle() = 0;
    
    virtual bool checkPixelPixelCollision(float px, float py, int &ix, int &iy) = 0;
    bool checkPixelPixelCollision(float px, float py) {
        int ix = 0, iy = 0;
        return checkPixelPixelCollision(px, py, ix, iy);
    }

    virtual bool checkCirclePixelCollision(float px, float py, float r) = 0;

    virtual bool getCollisionReflection(float px, float py, float dx, float dy, float &rx, float & ry) = 0;


    float itemRepeatTimer = 0, itemSecondaryRepeatTimer = 0;

    bool usingItemOnce = false;

    void useItem() {
        itemUsing = 1;
    }
    void stopUseItem() {
        itemUsing = 0;
    }
    void useItemOnce() {
        usingItemOnce = true;
        itemUsing = 1;
    }

    void useItemSecondary() {
        itemUsingSecondary = 1;
    }

    void stopUseItemSecondary()  {
        itemUsingSecondary = 0;
    }

    void jump() {
        if(readyToJump) {
            vy = -0.5*gravity;//*scaleY;
            readyToJump = false;
        }
    }

    int movement = 0;
    int aiming = 0;
    int itemUsing = 0, itemUsingSecondary = 0;

    float speedFactor = 1.0;

    void moveLeft() {
        if(itemChange == ItemChange::ItemChangeInited || itemChange == ItemChange::ItemChangeSecondary) {
            itemChange = ItemChange::ItemChangePrimary;
        }
        else if(itemChange == ItemChange::ItemChangePrimary) {
            activeItem -= 1;
            if(activeItem < 0) {
                activeItem = items.size()-1;
            }
        }
        else {
            vx = -speed*scaleX;
            direction = Direction::Left;
            movement = 1;
        }
    }
    void moveRight() {
        if(itemChange == ItemChange::ItemChangeInited || itemChange == ItemChange::ItemChangeSecondary) {
            itemChange = ItemChange::ItemChangePrimary;
        }
        else if(itemChange == ItemChange::ItemChangePrimary) {
            activeItem += 1;
            if(activeItem >= items.size()) {
                activeItem = 0;
            }
        }
        else {
            vx = speed*scaleX;
            direction = Direction::Right;
            movement = 2;
        }
    }
    void stopMoveLeft() {
        if(movement == 1) {
            vx = 0;
        }
    }
    void stopMoveRight() {
        if(movement == 2) {
            vx = 0;
        }
    }

    void aimUp() {
        if(itemChange == ItemChange::ItemChangeInited || itemChange == ItemChange::ItemChangePrimary) {
            itemChange = ItemChange::ItemChangeSecondary;
        }
        else if(itemChange == ItemChange::ItemChangeSecondary) {
            activeItemSecondary -= 1;
            if(activeItemSecondary < 0) {
                activeItemSecondary = itemsSecondary.size()-1;
            }
        }
        else {
            aiming = 1;
        }
    }
    void aimDown() {
        if(itemChange == ItemChange::ItemChangeInited || itemChange == ItemChange::ItemChangePrimary) {
            itemChange = ItemChange::ItemChangeSecondary;
        }
        else if(itemChange == ItemChange::ItemChangeSecondary) {
            activeItemSecondary += 1;
            if(activeItemSecondary >= itemsSecondary.size()) {
                activeItemSecondary = 0;
            }
        }
        else {
            aiming = 2;
        }
    }
    void stopAimUp() {
        if(aiming == 1) {
            aiming = 0;
        }
    }
    void stopAimDown() {
        if(aiming == 2) {
            aiming = 0;
        }
    }

    enum class ItemChange { None, ItemChangeInited, ItemChangePrimary, ItemChangeSecondary };
    ItemChange itemChange = ItemChange::None;


    void changeItem() {
        if(itemChange == ItemChange::None) {
            itemChange = ItemChange::ItemChangePrimary;
        }
        else {
            itemChange = ItemChange::None;
        }

        if(doubleClickedItemChangeTimer < 0) {
            doubleClickedItemChangeTimer = 0.0000001;
            doubleClickedItemChangeActive = -1;
        }
        else if(doubleClickedItemChangeTimer > 0) {
            if(doubleClickedItemChangeTimer <= doubleClickDurationSeconds) {
                doubleClickedItemChange = true;
                doubleClickedItemChangeActive = 2;
            }
            doubleClickedItemChangeTimer = -1;
        }
    }


    void renderCrosshair(sf::RenderWindow &window) {
        float dir = direction == Right ? 1 : -1;
        float cx = x;// + w/2;
        float cy = y;// + h/2;
        float cr = max(w*scaleX, h*scaleY) * 0.75;
        float hx = cx + dir * cos(aimAngle)*cr;
        float hy = cy + sin(aimAngle)*cr; 

        crosshairSprite.setPosition(hx, hy);
        crosshairSprite.setScale(scaleX, scaleY);
        window.draw(crosshairSprite);
    }


};








struct Walker : public Vehicle {


    struct ComputerControl {
        struct CheckPoint {
            float x = 0, y = 0;
            float radius = 50;
        };

        //enum class ControlState { Free, GoingToCheckPoint, ShootingOpponent, LoadingWeapon, GrowingLand, GoingToCheckPointAndDroppingNuclearBomb, AttackingOpponent, AttackingOpponentAndShooting, UsingMelterCannon, UsingEarthquake };
        enum class ControlState { Free, GoingToCheckPoint, ShootingOpponent, LoadingWeapon, AttackingOpponent, AttackingOpponentAndShooting, UsingMelterCannon };
        ControlState controlState = ControlState::Free;

        //enum class ControlStateSecondary { Free, Digging, UsingJetPack };
        enum class ControlStateSecondary { Free, Digging };
        ControlStateSecondary controlStateSecondary = ControlStateSecondary::Free;

        vector<Character*> otherCharacters;
        //Character *thisCharacter = nullptr;
        Walker *thisWalker = nullptr;
        Character *visibleOpponent = nullptr;
        Vehicle *visibleHostileVehicle = nullptr;
        float opponentLastSeenX = 0, opponentLastSeenY = 0;
        float opponentLastSeenTimer = 0;

        float probabilityOfStartingAnAttack = 1.0 / 5.0;

        Map *map = nullptr;

        vector<CheckPoint> checkPoints;

        //bool isActive = false;

        float opponentVisibilityCheckTimer = 0;
        float opponentVisibilityCheckDuration = 0.1;

        float checkPointCheckTimer = 0;
        float checkPointCheckDuration = 3;

        float diggingTimer = 0;
        float diggingDuration = 0.1;

        float deltaAimAngle = 0.0666 * Pi;

        float weaponLoadingTimer = 0;
        float weaponLoadingDuration = 1.5;

        float melterCannonReactionTime = 0;
        float melterCannonReactionTimeMin = 0.1, melterCannonReactionTimeMax = 1.2;
        float melterCannonDuration = 1.6;
        float melterCannonTimer = 0;
        ControlState controlStateBeforeMelterCannon = ControlState::Free;



        void init(Map *map, Walker *thisWalker);


        void update(float dt);

        sf::CircleShape cs;

        void render(sf::RenderWindow &window) {
            for(int i=0; i<checkPoints.size(); i++) {
                cs.setRadius(checkPoints[i].radius);
                cs.setFillColor(sf::Color(255, 0, 0, 40));
                cs.setPosition(sf::Vector2f(checkPoints[i].x - checkPoints[i].radius, checkPoints[i].y - checkPoints[i].radius));
                window.draw(cs);
            }
        }

        void onDeath() {
            if(controlState == ControlState::ShootingOpponent ||
               controlState == ControlState::LoadingWeapon ||
               controlState == ControlState::AttackingOpponent ||
               controlState == ControlState::AttackingOpponentAndShooting || 
               controlState == ControlState::UsingMelterCannon)
            {
                /*if(controlState == ControlState::UsingMelterCannon) {
                    thisCharacter->activeItem = 0;
                }
                controlState = ControlState::Free;

                checkPoints.clear();

                CheckPoint checkPoint;
                checkPoint.x = randf2(50, map->w * map->scaleX - 50);
                checkPoint.y = randf2(50, map->h * map->scaleY - 50);
                checkPoints.push_back(checkPoint);*/
            }
        }

        void changeWeapon();

        float checkGroundBetweenCharacterAndCheckPoint();

        float distanceToGroundBelowCharacter();

        bool isMelterCannonApplicable();

        bool isMelterCannonApplicableAgainstVehicles();

        void checkOpponentVisibility();

    };

    ComputerControl computerControl;






    static sf::SoundBuffer walkerExplosionSoundBuffer;
    static bool initialized;


    void setup(Map *map, const string &filenameBase, int team) {
        this->map = map;

        this->team = team;

        if(!initialized) {
            if(!walkerExplosionSoundBuffer.loadFromFile("data/audio/walkerExplosion2.ogg")) {
                printf("Couldn't open file 'data/audio/walkerExplosion2.ogg'!\n");
                return;
            }
            initialized = true;
        }

        std::string fileName = "data/textures/" + filenameBase+"_standing.png";
        if(!vehicleImageStanding[Direction::Right].loadFromFile(fileName)) {
            printf("Couldn't open file '%s'!\n", fileName.c_str());
            return;
        }
        /*if(!characterImage.loadFromFile(filename)) {
            printf("Couldn't open file '%s'!\n", filename.c_str());
            return;
        }*/
        sf::Vector2u size = vehicleImageStanding[Direction::Right].getSize();
        w = size.x;
        h = size.y;


        spritePositionDeltaY = 0;

        vehicleTextureStanding[Direction::Right].loadFromImage(vehicleImageStanding[Direction::Right]);
        vehicleSpriteStanding[Direction::Right].setTexture(vehicleTextureStanding[Direction::Right], true);
        vehicleSpriteStanding[Direction::Right].setOrigin(w/2, h/2 + spritePositionDeltaY);

        const sf::Uint8 *pixelsStandingRight = vehicleImageStanding[Direction::Right].getPixelsPtr();
        sf::Uint8 pixelsStandingLeft[w*h*4];
        for(int x=0; x<w; x++) {
            int px = w-1  - x;
            for(int y=0; y<h; y++) {
                pixelsStandingLeft[x*4+0 + y*w*4] = pixelsStandingRight[px*4+0 + y*w*4];
                pixelsStandingLeft[x*4+1 + y*w*4] = pixelsStandingRight[px*4+1 + y*w*4];
                pixelsStandingLeft[x*4+2 + y*w*4] = pixelsStandingRight[px*4+2 + y*w*4];
                pixelsStandingLeft[x*4+3 + y*w*4] = pixelsStandingRight[px*4+3 + y*w*4];
            }
        }
        vehicleImageStanding[Direction::Left].create(w, h, pixelsStandingLeft);
        vehicleTextureStanding[Direction::Left].loadFromImage(vehicleImageStanding[Direction::Left]);
        vehicleSpriteStanding[Direction::Left].setTexture(vehicleTextureStanding[Direction::Left], true);
        vehicleSpriteStanding[Direction::Left].setOrigin(w/2, h/2 + spritePositionDeltaY);


        numRunningAnimationFrames = 11;

        for(int i=0; i<2; i++) {
            vehicleImageRunning[i].resize(numRunningAnimationFrames);
            vehicleTextureRunning[i].resize(numRunningAnimationFrames);
            vehicleSpriteRunning[i].resize(numRunningAnimationFrames);
        }

        for(int i=0; i<numRunningAnimationFrames; i++) {
            std::string fileName = "data/textures/" + filenameBase + "_walking_" + std::to_string(i+1) + "of"+std::to_string(numRunningAnimationFrames)+".png";
            if(!vehicleImageRunning[Direction::Right][i].loadFromFile(fileName)) {
                printf("Couldn't open file '%s'!\n", fileName.c_str());
                return;
            }
            vehicleTextureRunning[Direction::Right][i].loadFromImage(vehicleImageRunning[Direction::Right][i]);
            vehicleSpriteRunning[Direction::Right][i].setTexture(vehicleTextureRunning[Direction::Right][i], true);
            vehicleSpriteRunning[Direction::Right][i].setOrigin(w*0.5, h*0.5 + spritePositionDeltaY);


            const sf::Uint8 *pixelsRunningRight = vehicleImageRunning[Direction::Right][i].getPixelsPtr();
            sf::Uint8 pixelsRunningLeft[w*h*4];
            for(int x=0; x<w; x++) {
                int px = w-1  - x;
                for(int y=0; y<h; y++) {
                    pixelsRunningLeft[x*4+0 + y*w*4] = pixelsRunningRight[px*4+0 + y*w*4];
                    pixelsRunningLeft[x*4+1 + y*w*4] = pixelsRunningRight[px*4+1 + y*w*4];
                    pixelsRunningLeft[x*4+2 + y*w*4] = pixelsRunningRight[px*4+2 + y*w*4];
                    pixelsRunningLeft[x*4+3 + y*w*4] = pixelsRunningRight[px*4+3 + y*w*4];
                }
            }
            vehicleImageRunning[Direction::Left][i].create(w, h, pixelsRunningLeft);
            vehicleTextureRunning[Direction::Left][i].loadFromImage(vehicleImageRunning[Direction::Left][i]);
            vehicleSpriteRunning[Direction::Left][i].setTexture(vehicleTextureRunning[Direction::Left][i], true);
            vehicleSpriteRunning[Direction::Left][i].setOrigin(w/2, h/2 + spritePositionDeltaY);

        }

        //pixelsVector.resize(2 * (1+numRunningAnimationFrames));

        pixelsVector.push_back(vehicleImageStanding[Direction::Right].getPixelsPtr());
        for(int i=0; i<numRunningAnimationFrames; i++) {
            pixelsVector.push_back(vehicleImageRunning[Direction::Right][i].getPixelsPtr());
        }
        pixelsVector.push_back(vehicleImageStanding[Direction::Left].getPixelsPtr());
        for(int i=0; i<numRunningAnimationFrames; i++) {
            pixelsVector.push_back(vehicleImageRunning[Direction::Left][i].getPixelsPtr());
        }


        x = randf(w, map->w*map->scaleX-w-1);
        y = map->h*map->scaleY/2;
        this->scaleX = map->scaleX;
        this->scaleY = map->scaleY;

        float r = max(w, h);
        int px = map->mapX(x, screenW);
        int py = map->mapY(y, screenH);

        for(int i=-r; i<r; i++) {
            if(px + i < 0 || px + i >= map->w) {
                continue;
            }
            for(int j=-r; j<r; j++) {
                if(py + j < 0 || py + j >= map->h) {
                    continue;
                }
                if(i*i + j*j < r*r) {
                    map->tiles[px+i + (py+j)*map->w] = map->emptyTile;
                }
            }
        }



        if(!crosshairImage.loadFromFile("data/textures/crosshair.png")) {
            printf("Couldn't open file 'data/textures/crosshair.png'!\n");
            return;
        }
        crosshairTexture.loadFromImage(crosshairImage);
        crosshairSprite.setTexture(crosshairTexture, true);

        sf::Vector2u crosshairSize = crosshairImage.getSize();
        crosshairSprite.setOrigin(crosshairSize.x*0.5, crosshairSize.y*0.5);

        

        HeavyFlamer *heavyFlamer = new HeavyFlamer();
        heavyFlamer->itemUserVehicle = this;
        items.push_back(heavyFlamer);

        LaserCannon *laserCannon = new LaserCannon();
        laserCannon->itemUserVehicle = this;
        items.push_back(laserCannon);

        ClusterMortar *clusterMortar = new ClusterMortar();
        clusterMortar->itemUserVehicle = this;
        items.push_back(clusterMortar);

        /*Bomb *bomb = new Bomb();
        bomb->itemUserVehicle = this; // TODO fix
        items.push_back(bomb);

        ClusterBomb *clusterBomb = new ClusterBomb();
        clusterBomb->itemUserVehicle = this;
        items.push_back(clusterBomb);

        Napalm *napalm = new Napalm();
        napalm->itemUserVehicle = this;
        items.push_back(napalm);

        FlameThrower *flameThrower = new FlameThrower();
        flameThrower->itemUserVehicle = this;
        items.push_back(flameThrower);

        LightningStrike *lightningStrike = new LightningStrike();
        lightningStrike->itemUserVehicle = this;
        items.push_back(lightningStrike);

        Blaster *blaster = new Blaster();
        blaster->itemUserVehicle = this;
        items.push_back(blaster);

        NuclearBomb *nuclearBomb = new NuclearBomb();
        nuclearBomb->itemUserVehicle = this;
        items.push_back(nuclearBomb);

        MissileLauncher *missileLauncher = new MissileLauncher();
        missileLauncher->itemUserVehicle = this;
        items.push_back(missileLauncher);

        ReflectorBeam *reflectorBeam = new ReflectorBeam();
        reflectorBeam->itemUserVehicle = this;
        items.push_back(reflectorBeam);

        BouncyBomb *bouncyBomb = new BouncyBomb();
        bouncyBomb->itemUserVehicle = this;
        items.push_back(bouncyBomb);

        Rifle * = new Rifle();
        rifle->itemUserVehicle = this;
        items.push_back(rifle);

        Shotgun *shotgun = new Shotgun();
        shotgun->itemUserVehicle = this;
        items.push_back(shotgun);

        DoomsDay *doomsDay = new DoomsDay();
        doomsDay->itemUserVehicle = this;
        items.push_back(doomsDay);*/

        Bolter *bolter = new Bolter();
        bolter->itemUserVehicle = this;
        bolter->name = "Multi Bolter";
        bolter->numBolts = 5;
        bolter->repeatTime_ = 0.25;
        bolter->explosionRadius = 30;
        bolter->manaCostPerUse_ = 100.0 / 16.0;
        bolter->loadingTimeSeconds_ = 10;
        items.push_back(bolter);

        /*Railgun *railgun = new Railgun();
        railgun->itemUserVehicle = this;
        items.push_back(railgun);*/

        DirtCannon *dirtCannon = new DirtCannon();
        dirtCannon->itemUserVehicle = this;
        dirtCannon->range = 500;
        dirtCannon->deltaAngle = 0.1 * Pi;
        dirtCannon->numWarningProjectiles = 1000;
        dirtCannon->name = "Heavy Melter Cannon";
        items.push_back(dirtCannon);

        /*Earthquake *earthquake = new Earthquake();
        earthquake->itemUserVehicle = this;
        items.push_back(earthquake);*/


        Digger *digger = new Digger();
        digger->itemUserVehicle = this;
        itemsSecondary.push_back(digger);

        /*JetPack *jetPack = new JetPack();
        jetPack->itemUserVehicle = this;
        itemsSecondary.push_back(jetPack);

        LandGrower *rockGrower = new LandGrower(true);
        rockGrower->itemUserVehicle = this;
        itemsSecondary.push_back(rockGrower);

        LandGrower *woodGrower = new LandGrower(false);
        woodGrower->itemUserVehicle = this;
        itemsSecondary.push_back(woodGrower);

        Repeller *repeller = new Repeller();
        repeller->itemUserVehicle = this;
        itemsSecondary.push_back(repeller);*/

        LaserSight *laserSight = new LaserSight();
        laserSight->itemUserVehicle = this;
        itemsSecondary.push_back(laserSight);

        maxHp = 1000;
        hp = 1000;

        activeItem = 3;


        collisionHandler.init(*this);

        computerControl.init(map, this);

    }

    void updatePixelsVectorIndex() {
        if(direction == Direction::Right) {
            if(running) {
                pixelsVectorIndex = 1 + runningAnimationCounter;
            }
            else {
                pixelsVectorIndex = 0;
            }
        }
        else {
            if(running) {
                pixelsVectorIndex = 1 + numRunningAnimationFrames + 1 + runningAnimationCounter;
            }
            else {
                pixelsVectorIndex = 1 + numRunningAnimationFrames;
            }
        }
    }


    struct CollisionPoint {
        float angle = 0;
        float dx = 0, dy = 0;

    };

    struct CollisionHandler {
        int numCollisionPoints = 32;
        vector<CollisionPoint> collisionPoints;
        sf::RectangleShape rect;

        CollisionPoint collidingPoint;
        bool isCollision = false;

        void renderCollisionPoints(sf::RenderWindow &window, Walker &walker) {
            for(int i=0; i<numCollisionPoints; i++) {
                //rect.setPosition(character.x + 0.5 * character.w * character.scaleX + collisionPoints[i].dx, character.y + 0.5* character.h * character.scaleY + collisionPoints[i].dy); TODO remove this

                rect.setPosition(walker.x + collisionPoints[i].dx, walker.y + collisionPoints[i].dy);

                if(collisionPoints[i].angle >= 0.25 * Pi && collisionPoints[i].angle < 0.75 * Pi) {
                    rect.setFillColor(sf::Color(255, 0, 0, 255));
                }
                if(collisionPoints[i].angle >= 0.75 * Pi && collisionPoints[i].angle < 1.25 * Pi) {
                    rect.setFillColor(sf::Color(255, 255, 0, 255));
                }
                if(collisionPoints[i].angle >= 1.25 * Pi && collisionPoints[i].angle < 1.75 * Pi) {
                    rect.setFillColor(sf::Color(255, 0, 255, 255));
                }
                if((collisionPoints[i].angle >= 1.75 * Pi && collisionPoints[i].angle < 2.0 * Pi) || (collisionPoints[i].angle >= 0.0 * Pi &&
                collisionPoints[i].angle < 0.25 * Pi)) {
                    rect.setFillColor(sf::Color(0, 255, 0, 255));
                }

                window.draw(rect);
            }
        }

        void init(Walker &walker) {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(walker.scaleX, walker.scaleY);
            rect.setOrigin(walker.scaleX*0.5, walker.scaleY*0.5);


            float initialAngle = 0.5*Pi;
            float deltaAngle = 2.0*Pi / numCollisionPoints;

            float radiusX = walker.w*walker.scaleX * 0.5;
            float radiusY = walker.h*walker.scaleY * 0.5;

            for(int i=0; i<numCollisionPoints; i++) {
                CollisionPoint collisionPoint;
                collisionPoint.angle = fmod(initialAngle + i * deltaAngle, 2.0*Pi);
                collisionPoint.dx = radiusX * cos(collisionPoint.angle);
                collisionPoint.dy = radiusY * sin(collisionPoint.angle);
                collisionPoints.push_back(collisionPoint);
            }
        }

        void handleCollisions(Walker &walker, Map &map) {
            isCollision = false;

            for(int p=0; p<numCollisionPoints; p++) {
                //int i = 0;
                int i = p % numCollisionPoints;
                int j = (i + numCollisionPoints/2) % numCollisionPoints;
                /*float x = character.x + character.w*character.scaleX*0.5;
                float y = character.y + character.h*character.scaleY*0.5;
                //TODO remove this */

                float x = walker.x;
                float y = walker.y;

                float dx = cos(collisionPoints[i].angle);
                float dy = sin(collisionPoints[i].angle);

                bool iCollides = false, jCollides = false;
                bool buriedInGround = false;
                float tix = 0, tiy = 0;
                float tjx = 0, tjy = 0;


                iCollides = map.checkCollision(x + collisionPoints[i].dx, y + collisionPoints[i].dy);

                //if(iCollides && fabs(collisionPoints[i].dx) >= character.w*character.scaleX*0.1) {
                if(iCollides && collisionPoints[i].dy <= walker.h*walker.scaleY*0.30) {
                    // TODO check this!
                    if(!isCollision) {
                        collidingPoint = collisionPoints[i];
                    }
                    isCollision = true;
                }

                jCollides = map.checkCollision(x + collisionPoints[j].dx, y + collisionPoints[j].dy);
                int k = 0;
                while(iCollides) {
                    tix = -k * dx;
                    tiy = -k * dy;
                    tjx = -k * dx;
                    tjy = -k * dy;
                    iCollides = map.checkCollision(x + collisionPoints[i].dx + tix, y + collisionPoints[i].dy + tiy);
                    jCollides = map.checkCollision(x + collisionPoints[j].dx + tjx, y + collisionPoints[j].dy + tjy);
                    if(iCollides && jCollides) {
                        buriedInGround = true;
                        break;
                    }
                    if(!iCollides && !jCollides) {
                        break;
                    }
                    k++;
                }
                if(buriedInGround) {
                    //printf("buriedInGround i = %d\n", i);
                    //break;
                }
                if(!iCollides && !jCollides) {
                    //break;
                }
                struct P {
                    int k = 0;
                    int dx = 0;
                    int dy = 0;
                    float getMagnitude() {
                        return sqrt(dx*dx + dy*dy);
                    }
                };
                if(buriedInGround && i == 0) {
                    walker.vy = 0;
                    //break;
                    P pUp, pDown, pLeft, pRight;
                    bool upCollides = true;
                    bool downCollides = true;
                    bool leftCollides = true;
                    bool rightCollides = true;
                    while(upCollides) {
                        pUp.dx = 0;
                        pUp.dy = -pUp.k;
                        upCollides = map.checkCollision(x + pUp.dx, y + pUp.dy);
                        pUp.k++;
                        if(pUp.k > 500) break;
                    }
                    //printf("pUp.k %d\n", pUp.k);

                    while(downCollides) {
                        pDown.dx = 0;
                        pDown.dy = pDown.k;
                        downCollides = map.checkCollision(x + pDown.dx, y + pDown.dy);
                        pDown.k++;
                        if(pDown.k > 500) break;
                    }
                    //printf("pDown.k %d\n", pDown.k);

                    while(leftCollides) {
                        pLeft.dx = -pLeft.k;
                        pLeft.dy = 0;
                        leftCollides = map.checkCollision(x + pLeft.dx, y + pLeft.dy);
                        pLeft.k++;
                        if(pLeft.k > 500) break;
                    }
                    //printf("pLeft.k %d\n", pLeft.k);

                    while(rightCollides) {
                        pRight.dx = pRight.k;
                        pRight.dy = 0;
                        rightCollides = map.checkCollision(x + pRight.dx, y + pRight.dy);
                        pRight.k++;
                        if(pRight.k > 500) break;
                    }
                    //printf("pRight.k %d\n", pRight.k);

                    if(pLeft.getMagnitude() <= pUp.getMagnitude() && pLeft.getMagnitude() <= pDown.getMagnitude() && pLeft.getMagnitude() <= pRight.getMagnitude()) {
                        tix = pLeft.dx;
                        tiy = pLeft.dy;
                    }
                    else if(pRight.getMagnitude() <= pUp.getMagnitude() && pRight.getMagnitude() <= pDown.getMagnitude() && pRight.getMagnitude() <= pLeft.getMagnitude()) {
                        tix = pRight.dx;
                        tiy = pRight.dy;
                    }
                    else if(pDown.getMagnitude() <= pUp.getMagnitude()) {
                        tix = pDown.dx;
                        tiy = pDown.dy;
                    }
                    else {
                        tix = pUp.dx;
                        tiy = pUp.dy;
                    }
                    walker.x += tix;
                    walker.y += tiy;

                    //break;
                }
                else {
                    //character.x += tix;
                    //character.y += tiy;

                    if(fabs(tix) >= 8) {
                        walker.x += tix;
                    }
                    else if(fabs(tiy) < 0.001) {
                        walker.x += tix;
                    }
                    if(fabs(tiy) >= 8) {
                        walker.y += tiy;
                    }
                    else if(fabs(tix) < 0.001) {
                        walker.y += tiy;
                    }
                    /*if(fabs(tix) < 3 && fabs(tiy) < 0.001) {
                        character.x += tix;
                    }
                    if(fabs(tiy) <= 3  && fabs(tiy) < 0.001) {
                        character.y += tiy;
                    }*/
                    if(tiy < 0 && fabs(tix) < 0.001) {
                        walker.readyToJump = true;
                        walker.vy = 0;
                    }
                    if(tiy > 0 && fabs(tix) < 0.001) {
                        walker.vy = 0;
                    }
                }
            }

            // TODO fix the hack below:
            if(walker.x - walker.w * map.scaleX * 0.5 < 0) {
                walker.x = walker.w * map.scaleX * 0.5;
            }
            if(walker.y - walker.h * map.scaleY * 0.5 < 0) {
                walker.y = walker.h * map.scaleY * 0.5;
            }
            if(walker.x + walker.w * map.scaleX * 0.5 > map.w * map.scaleX) {
                walker.x = map.w * map.scaleX - walker.w * map.scaleX * 0.5;
            }
            if(walker.y + walker.h * map.scaleY * 0.5 > map.h * map.scaleY) {
                walker.y = map.h * map.scaleY - walker.h * map.scaleY * 0.5;
            }
        }
    };

    CollisionHandler collisionHandler;







    struct EnterVehicleProjectile : public Projectile {
        //static sf::RectangleShape rect;

        sf::Color color, color2;

        float time = 0;
        float duration = 1;

        static void prepare() {
        }

        void update(Map& map, float dt) {
            if(gravityHasAnEffect) {
                vy += gravity * dt;
            }
            x += vx * dt;
            y += vy * dt;

            time += dt;

            if(time >= duration) {
                canBeRemoved = true;
                return;
            }
        }


        void render(sf::RenderWindow &window, float scaleX, float scaleY) {

        }

        sf::Color getPixelColor() {
            return mix(color, color2, time/duration);
        }

        virtual std::string getName() {
            return "Enter Vehicle Projectile";
        }
    };

    bool renderEnterHalo = false;









    void update(float dt, int screenW, int screenH, Map &map) {
        dt = dt > 1.0/60.0 ? 1.0/60.0 : dt;


        /*if(respawning) {
            respawnTimer += dt;
            x = lerp(pointOfDeathX, respawnX, respawnTimer / respawnDuration);
            y = lerp(pointOfDeathY, respawnY, respawnTimer / respawnDuration);
            hp = lerp(0, maxHp, respawnTimer / respawnDuration);

            if(respawnTimer >= respawnDuration) {
                respawning = false;
                respawnTimer = 0;

                float tx = x;
                float ty = y;

                float r = 30;
                int px = map.mapX(x, screenW);
                int py = map.mapY(y, screenH);

                for(int i=-r; i<r; i++) {
                    if(px + i < 0 || px + i >= map.w) {
                        continue;
                    }
                    for(int j=-r; j<r; j++) {
                        if(py + j < 0 || py + j >= map.h) {
                            continue;
                        }
                        if(i*i + j*j < r*r) {
                            map.tiles[px+i + (py+j)*map.w] = map.emptyTile;
                        }
                    }
                }
            }
            else {
                return;
            }
        }*/


        centerX = x;
        centerY = y;    // TODO remove this


        computerControl.update(dt);
        globalAngle = direction == Direction::Right ? aimAngle : Pi - aimAngle;


        itemRepeatTimer += dt;

        if(itemUsing == 1 && itemRepeatTimer >= items[activeItem]->repeatTime() && activeItem >= 0 && activeItem < items.size()) {
            float a = direction == Direction::Right ? aimAngle : Pi - aimAngle;
            if(items[activeItem]->noManaCostPerUse()) {
                items[activeItem]->use(x, y, vx, vy, a);
                itemRepeatTimer = 0;
            }
            else {
                if(items[activeItem]->itemMana >= items[activeItem]->manaCostPerUse()) {
                    items[activeItem]->use(x, y, vx, vy, a);
                    items[activeItem]->itemMana -= items[activeItem]->manaCostPerUse();
                    itemRepeatTimer = 0;
                }
            }
            if(usingItemOnce) {
                usingItemOnce = false;
                itemUsing = 0;
            }
        }

        itemSecondaryRepeatTimer += dt;

        if(itemUsingSecondary == 1 && itemSecondaryRepeatTimer >= itemsSecondary[activeItemSecondary]->repeatTime() && activeItemSecondary >= 0 && activeItemSecondary < itemsSecondary.size()) {
            float a = direction == Direction::Right ? aimAngle : Pi - aimAngle;
            if(itemsSecondary[activeItemSecondary]->noManaCostPerUse()) {
                itemsSecondary[activeItemSecondary]->use(x, y, vx, vy, a);
                itemSecondaryRepeatTimer = 0;
            }
            else {
                if(itemsSecondary[activeItemSecondary]->itemMana >= itemsSecondary[activeItemSecondary]->manaCostPerUse()) {
                    itemsSecondary[activeItemSecondary]->use(x, y, vx, vy, a);
                    itemsSecondary[activeItemSecondary]->itemMana -= itemsSecondary[activeItemSecondary]->manaCostPerUse();
                    itemSecondaryRepeatTimer = 0;
                }
            }
        }

        items[activeItem]->update(map, dt);
        itemsSecondary[activeItemSecondary]->update(map, dt);

        for(int i=0; i<items.size(); i++) {
            if(i == activeItem) continue;
            items[i]->updateNonActive(map, dt);
        }
        for(int i=0; i<itemsSecondary.size(); i++) {
            if(i == activeItemSecondary) continue;
            itemsSecondary[i]->updateNonActive(map, dt);
        }

        for(int i=0; i<items.size(); i++) {
            items[i]->loadMana(map, dt);
        }
        for(int i=0; i<itemsSecondary.size(); i++) {
            itemsSecondary[i]->loadMana(map, dt);
        }

        items[activeItem]->itemMana -= items[activeItem]->manaCostPerSecond() * dt;


        if(aiming == 1) {
            aimAngle -= dt*Pi * 0.5;
            if(aimAngle <= -Pi*0.5) {
                aimAngle = -Pi*0.5;
            }
        }
        if(aiming == 2) {
            aimAngle += dt*Pi * 0.5;
            if(aimAngle >= Pi*0.5) {
                aimAngle = Pi*0.5;
            }
        }
        globalAngle = direction == Direction::Right ? aimAngle : Pi - aimAngle;


        float ax = 0;
        float ay = 0;
        ay += gravity;

        vx += ax*dt;
        vy += ay*dt;

        x += vx * dt * speedFactor;
        y += vy * dt * speedFactor;

        speedFactor = 1.0; // TODO fix this hack



        collisionHandler.handleCollisions(*this, map);


        afterUpdate();

        float tx = x + (direction == Direction::Right ? - 11*scaleX : 11*scaleX);
        float ty = y - 22*scaleY;

        for(int i=0; i<1; i++) {
            createSmoke(tx-2, ty, map.characters, 10, 10, 100);
        }
        if(renderEnterHalo) {
            for(int i=0; i<20; i++) {
                EnterVehicleProjectile *evp = new EnterVehicleProjectile();
                float angle = randf2(0, 2.0*Pi);
                float radius = randf2(2, 2.2) * max(w, h);
                evp->x = x + radius * cos(angle);
                evp->y = y + radius * sin(angle);
                if(i % 2 == 0) {
                    evp->vx = randf2(-1, 1);
                    evp->vy = randf2(-1, 1);
                }
                else {
                    evp->vx = -cos(angle) * 50;
                    evp->vy = -sin(angle) * 50;
                }
                evp->gravityHasAnEffect = false;
                evp->duration = randf2(1, 5);
                evp->color = sf::Color(24, 143, 255, randi(30, 255));
                evp->color2 = sf::Color(0, 0, 0, 0);
                evp->characters = map.characters;
                evp->projectileUserVehicle = this;
                projectiles.push_back(evp);
            }
        }

        if(doubleClickedItemChange) {
            exitThisVehicle();
        }

        if(doubleClickedItemChangeTimer >= 0) {
            doubleClickedItemChangeTimer += dt;
        }
        if(doubleClickedItemChangeActive > 0) {
            doubleClickedItemChangeActive--;
            if(doubleClickedItemChangeActive == 0) {
                doubleClickedItemChange = false;
            }
        }
    }



    void afterUpdate(bool forceInactive = false) {
        if(activeItem >= 0 && activeItem < items.size()) {
            items[activeItem]->afterUpdate(forceInactive);
        }
        if(activeItemSecondary >= 0 && activeItemSecondary < itemsSecondary.size()) {
            itemsSecondary[activeItemSecondary]->afterUpdate(forceInactive);
        }
    }


    /*bool respawning = false;
    float respawnTimer = 0;
    float respawnDuration = 5.0;
    float respawnX = 0;
    float respawnY = 0;
    float pointOfDeathX = 0;
    float pointOfDeathY = 0;*/

    void takeDamage(float amount);

    void render(sf::RenderWindow &window);

    /*void renderCrosshair(sf::RenderWindow &window) {
        float dir = direction == Right ? 1 : -1;
        float cx = x;// + w/2;
        float cy = y;// + h/2;
        float cr = 25*scaleX;
        float hx = cx + dir * cos(aimAngle)*cr;
        float hy = cy + sin(aimAngle)*cr; //TODO remove this

        crosshairSprite.setPosition(hx, hy);
        crosshairSprite.setScale(scaleX, scaleY);
        window.draw(crosshairSprite);
    }*/

    bool getCollisionNormal(float px, float py, float &nx, float &ny) {
        float ax = x - w*scaleX * 0.5;
        float ay = y - h*scaleY * 0.5;
        float bx = x + w*scaleX * 0.5;
        float by = y + h*scaleY * 0.5;

        if(!isWithinRect(px, py, ax, ay, bx, by)) {
            return false;
        }

        float dx = px - x;
        float dy = py - y;
        if(dx == 0 && dy == 0) {
            nx = 0;
            ny = 0;
        }
        else {
            float d = sqrt(dx*dx + dy*dy);
            nx = dx / d;
            ny = dy / d;
        }

        return true;
    }

    bool getCollisionReflection(float px, float py, float dx, float dy, float &rx, float & ry) {
        float nx = 0, ny = 0;
        bool collided = getCollisionNormal(px, py, nx, ny);
        if(collided) {
            //float d = sqrt(vx*vx + vy*vy);
            reflectVector(dx, dy, nx, ny, rx, ry);
        }
        return collided;
    }




    bool checkPixelPixelCollision(float px, float py, int &ix, int &iy) {
        ix = px - x + w*scaleX*0.5;
        ix = ix / scaleX;
        iy = py - y + h*scaleY*0.5;
        iy = iy / scaleY + spritePositionDeltaY;

        if(ix < 0) {
            return false;
        }
        if(iy < 0) {
            return false;
        }
        if(ix >= w) {
            return false;
        }
        if(iy >= h) {
            return false;
        }

        // TODO check the animation index
        if(pixelsVector.size() <= pixelsVectorIndex) {
            printf("At Walker::checkPixelPixelCollision(). This vector should be prepared in setup()...\n");
        }
        else if(pixelsVector[pixelsVectorIndex][ix*4 + iy*w*4 + 3] > 200) {              
            /*if(running) {
                vehicleImageRunning[direction][runningAnimationCounter].setPixel(ix, iy, sf::Color(255, 0, 0, 255));
                vehicleTextureRunning[direction][runningAnimationCounter].loadFromImage(vehicleImageRunning[direction][runningAnimationCounter]);
            }
            else {    
                vehicleImageStanding[direction].setPixel(ix, iy, sf::Color(255, 0, 0, 255));
                vehicleTextureStanding[direction].loadFromImage(vehicleImageStanding[direction]);
            }*/
            return true;
        }

        return false;
    }


    bool checkCirclePixelCollision(float px, float py, float r) {
        if(pixelsVector.size() <= pixelsVectorIndex) {
            printf("At Walker::checkCirclePixelCollision(). This vector should be prepared in setup()...\n");
             return false;
        }

        int ix = px - x + w*scaleX*0.5;
        ix = ix / scaleX;
        int iy = py - y + h*scaleY*0.5;
        iy = iy / scaleY + spritePositionDeltaY;

        r = r/scaleX;

        if(ix < -r) {
            return false;
        }
        if(iy < -r) {
            return false;
        }
        if(ix >= w+r) {
            return false;
        }
        if(iy >= h+r) {
            return false;
        }

        int rr = r*r;
        bool hit = false;


        for(int tx=0; tx<w; tx++) {
            for(int ty=0; ty<h; ty++) {
                if(pixelsVector[pixelsVectorIndex][tx*4 + ty*w*4 + 3] > 200) {
                    int dx = tx - ix;
                    int dy = ty - iy;
                    int dd = dx*dx + dy*dy;
                    if(dd <= rr) {
                        hit = true;
                        return true;
                        /*if(running) {
                            vehicleImageRunning[direction][runningAnimationCounter].setPixel(tx, ty, sf::Color(255, 0, 0, 255));
                            vehicleTextureRunning[direction][runningAnimationCounter].loadFromImage(vehicleImageRunning[direction][runningAnimationCounter]);
                        }
                        else {    
                            vehicleImageStanding[direction].setPixel(tx, ty, sf::Color(255, 0, 0, 255));
                            vehicleTextureStanding[direction].loadFromImage(vehicleImageStanding[direction]);
                        }*/
                    }
                }
            }
        }
        return hit;
    }






    void enterThisVehicle(const std::vector<Character*> &characters);

    void exitThisVehicle();
};

sf::SoundBuffer Walker::walkerExplosionSoundBuffer;
bool Walker::initialized = false;



vector<Vehicle*> vehicles;














struct Robot : public Vehicle {


    struct ComputerControl {
        struct CheckPoint {
            float x = 0, y = 0;
            float radius = 50;
        };

        //enum class ControlState { Free, GoingToCheckPoint, ShootingOpponent, LoadingWeapon, GrowingLand, GoingToCheckPointAndDroppingNuclearBomb, AttackingOpponent, AttackingOpponentAndShooting, UsingMelterCannon, UsingEarthquake };
        enum class ControlState { Free, GoingToCheckPoint, ShootingOpponent, LoadingWeapon, AttackingOpponent, AttackingOpponentAndShooting, UsingMelterCannon };
        ControlState controlState = ControlState::Free;

        enum class ControlStateSecondary { Free, Digging, UsingJetPack };
        //enum class ControlStateSecondary { Free, Digging };
        ControlStateSecondary controlStateSecondary = ControlStateSecondary::Free;

        vector<Character*> otherCharacters;
        //Character *thisCharacter = nullptr;
        Robot *thisRobot = nullptr;
        Character *visibleOpponent = nullptr;
        Vehicle *visibleHostileVehicle = nullptr;
        float opponentLastSeenX = 0, opponentLastSeenY = 0;
        float opponentLastSeenTimer = 0;

        float probabilityOfStartingAnAttack = 3.0 / 5.0;

        Map *map = nullptr;

        vector<CheckPoint> checkPoints;

        bool isActive = false;

        float opponentVisibilityCheckTimer = 0;
        float opponentVisibilityCheckDuration = 0.1;

        float checkPointCheckTimer = 0;
        float checkPointCheckDuration = 3;

        float diggingTimer = 0;
        float diggingDuration = 0.1;

        float jetPackTimer = 0;
        float jetPackDuration = 0.1;

        float deltaAimAngle = 0.0666 * Pi;

        float weaponLoadingTimer = 0;
        float weaponLoadingDuration = 1.5;

        /*float melterCannonReactionTime = 0;
        float melterCannonReactionTimeMin = 0.1, melterCannonReactionTimeMax = 1.2;
        float melterCannonDuration = 1.6;
        float melterCannonTimer = 0;
        ControlState controlStateBeforeMelterCannon = ControlState::Free;*/



        void init(Map *map, Robot *thisRobot);


        void update(float dt);

        sf::CircleShape cs;

        void render(sf::RenderWindow &window) {
            return;

            for(int i=0; i<checkPoints.size(); i++) {
                cs.setRadius(checkPoints[i].radius);
                cs.setFillColor(sf::Color(255, 0, 0, 40));
                cs.setPosition(sf::Vector2f(checkPoints[i].x - checkPoints[i].radius, checkPoints[i].y - checkPoints[i].radius));
                window.draw(cs);
            }
        }

        void onDeath() {
            if(controlState == ControlState::ShootingOpponent ||
               controlState == ControlState::LoadingWeapon ||
               controlState == ControlState::AttackingOpponent ||
               controlState == ControlState::AttackingOpponentAndShooting || 
               controlState == ControlState::UsingMelterCannon)
            {
                /*if(controlState == ControlState::UsingMelterCannon) {
                    thisCharacter->activeItem = 0;
                }
                controlState = ControlState::Free;

                checkPoints.clear();

                CheckPoint checkPoint;
                checkPoint.x = randf2(50, map->w * map->scaleX - 50);
                checkPoint.y = randf2(50, map->h * map->scaleY - 50);
                checkPoints.push_back(checkPoint);*/
            }
        }

        void changeWeapon();

        float checkGroundBetweenCharacterAndCheckPoint();

        float distanceToGroundBelowCharacter();

        bool isMelterCannonApplicable();

        bool isMelterCannonApplicableAgainstVehicles();

        void checkOpponentVisibility();

    };

    ComputerControl computerControl;


    bool remoteControlActive = false;



    static sf::SoundBuffer robotExplosionSoundBuffer;
    static bool initialized;


    void setup(Map *map, const string &filenameBase, int team) {
        this->map = map;

        this->team = team;

        if(!initialized) {
            if(!robotExplosionSoundBuffer.loadFromFile("data/audio/robotExplosion.ogg")) {
                printf("Couldn't open file 'data/audio/robotExplosion.ogg'!\n");
                return;
            }
            initialized = true;
        }

        std::string fileName = "data/textures/" + filenameBase+"_standing.png";
        if(!vehicleImageStanding[Direction::Right].loadFromFile(fileName)) {
            printf("Couldn't open file '%s'!\n", fileName.c_str());
            return;
        }
        /*if(!characterImage.loadFromFile(filename)) {
            printf("Couldn't open file '%s'!\n", filename.c_str());
            return;
        }*/
        sf::Vector2u size = vehicleImageStanding[Direction::Right].getSize();
        w = size.x;
        h = size.y;


        spritePositionDeltaY = 0;

        vehicleTextureStanding[Direction::Right].loadFromImage(vehicleImageStanding[Direction::Right]);
        vehicleSpriteStanding[Direction::Right].setTexture(vehicleTextureStanding[Direction::Right], true);
        vehicleSpriteStanding[Direction::Right].setOrigin(w/2, h/2 + spritePositionDeltaY);

        const sf::Uint8 *pixelsStandingRight = vehicleImageStanding[Direction::Right].getPixelsPtr();
        sf::Uint8 pixelsStandingLeft[w*h*4];
        for(int x=0; x<w; x++) {
            int px = w-1  - x;
            for(int y=0; y<h; y++) {
                pixelsStandingLeft[x*4+0 + y*w*4] = pixelsStandingRight[px*4+0 + y*w*4];
                pixelsStandingLeft[x*4+1 + y*w*4] = pixelsStandingRight[px*4+1 + y*w*4];
                pixelsStandingLeft[x*4+2 + y*w*4] = pixelsStandingRight[px*4+2 + y*w*4];
                pixelsStandingLeft[x*4+3 + y*w*4] = pixelsStandingRight[px*4+3 + y*w*4];
            }
        }
        vehicleImageStanding[Direction::Left].create(w, h, pixelsStandingLeft);
        vehicleTextureStanding[Direction::Left].loadFromImage(vehicleImageStanding[Direction::Left]);
        vehicleSpriteStanding[Direction::Left].setTexture(vehicleTextureStanding[Direction::Left], true);
        vehicleSpriteStanding[Direction::Left].setOrigin(w/2, h/2 + spritePositionDeltaY);


        numRunningAnimationFrames = 7;

        for(int i=0; i<2; i++) {
            vehicleImageRunning[i].resize(numRunningAnimationFrames);
            vehicleTextureRunning[i].resize(numRunningAnimationFrames);
            vehicleSpriteRunning[i].resize(numRunningAnimationFrames);
        }

        for(int i=0; i<numRunningAnimationFrames; i++) {
            std::string fileName = "data/textures/" + filenameBase + "_running_" + std::to_string(i+1) + "of"+std::to_string(numRunningAnimationFrames)+".png";
            if(!vehicleImageRunning[Direction::Right][i].loadFromFile(fileName)) {
                printf("Couldn't open file '%s'!\n", fileName.c_str());
                return;
            }
            vehicleTextureRunning[Direction::Right][i].loadFromImage(vehicleImageRunning[Direction::Right][i]);
            vehicleSpriteRunning[Direction::Right][i].setTexture(vehicleTextureRunning[Direction::Right][i], true);
            vehicleSpriteRunning[Direction::Right][i].setOrigin(w*0.5, h*0.5 + spritePositionDeltaY);


            const sf::Uint8 *pixelsRunningRight = vehicleImageRunning[Direction::Right][i].getPixelsPtr();
            sf::Uint8 pixelsRunningLeft[w*h*4];
            for(int x=0; x<w; x++) {
                int px = w-1  - x;
                for(int y=0; y<h; y++) {
                    pixelsRunningLeft[x*4+0 + y*w*4] = pixelsRunningRight[px*4+0 + y*w*4];
                    pixelsRunningLeft[x*4+1 + y*w*4] = pixelsRunningRight[px*4+1 + y*w*4];
                    pixelsRunningLeft[x*4+2 + y*w*4] = pixelsRunningRight[px*4+2 + y*w*4];
                    pixelsRunningLeft[x*4+3 + y*w*4] = pixelsRunningRight[px*4+3 + y*w*4];
                }
            }
            vehicleImageRunning[Direction::Left][i].create(w, h, pixelsRunningLeft);
            vehicleTextureRunning[Direction::Left][i].loadFromImage(vehicleImageRunning[Direction::Left][i]);
            vehicleSpriteRunning[Direction::Left][i].setTexture(vehicleTextureRunning[Direction::Left][i], true);
            vehicleSpriteRunning[Direction::Left][i].setOrigin(w/2, h/2 + spritePositionDeltaY);

        }

        //pixelsVector.resize(2 * (1+numRunningAnimationFrames));

        pixelsVector.push_back(vehicleImageStanding[Direction::Right].getPixelsPtr());
        for(int i=0; i<numRunningAnimationFrames; i++) {
            pixelsVector.push_back(vehicleImageRunning[Direction::Right][i].getPixelsPtr());
        }
        pixelsVector.push_back(vehicleImageStanding[Direction::Left].getPixelsPtr());
        for(int i=0; i<numRunningAnimationFrames; i++) {
            pixelsVector.push_back(vehicleImageRunning[Direction::Left][i].getPixelsPtr());
        }


        x = randf(w, map->w*map->scaleX-w-1);
        y = map->h*map->scaleY/2;
        this->scaleX = map->scaleX;
        this->scaleY = map->scaleY;

        float r = max(w, h);
        int px = map->mapX(x, screenW);
        int py = map->mapY(y, screenH);

        for(int i=-r; i<r; i++) {
            if(px + i < 0 || px + i >= map->w) {
                continue;
            }
            for(int j=-r; j<r; j++) {
                if(py + j < 0 || py + j >= map->h) {
                    continue;
                }
                if(i*i + j*j < r*r) {
                    map->tiles[px+i + (py+j)*map->w] = map->emptyTile;
                }
            }
        }



        if(!crosshairImage.loadFromFile("data/textures/crosshair.png")) {
            printf("Couldn't open file 'data/textures/crosshair.png'!\n");
            return;
        }
        crosshairTexture.loadFromImage(crosshairImage);
        crosshairSprite.setTexture(crosshairTexture, true);

        sf::Vector2u crosshairSize = crosshairImage.getSize();
        crosshairSprite.setOrigin(crosshairSize.x*0.5, crosshairSize.y*0.5);

        

        /*HeavyFlamer *heavyFlamer = new HeavyFlamer();
        heavyFlamer->itemUserVehicle = this;
        items.push_back(heavyFlamer);

        LaserCannon *laserCannon = new LaserCannon();
        laserCannon->itemUserVehicle = this;
        items.push_back(laserCannon);

        ClusterMortar *clusterMortar = new ClusterMortar();
        clusterMortar->itemUserVehicle = this;
        items.push_back(clusterMortar);*/

        /*Bomb *bomb = new Bomb();
        bomb->itemUserVehicle = this; // TODO fix
        items.push_back(bomb);

        ClusterBomb *clusterBomb = new ClusterBomb();
        clusterBomb->itemUserVehicle = this;
        items.push_back(clusterBomb);

        Napalm *napalm = new Napalm();
        napalm->itemUserVehicle = this;
        items.push_back(napalm);

        FlameThrower *flameThrower = new FlameThrower();
        flameThrower->itemUserVehicle = this;
        items.push_back(flameThrower);

        LightningStrike *lightningStrike = new LightningStrike();
        lightningStrike->itemUserVehicle = this;
        items.push_back(lightningStrike);*/

        Blaster *blaster = new Blaster();
        blaster->useOneBoltOnly = true;
        blaster->repeatTime_ = 0.2;
        blaster->team = team;
        blaster->ignoreTeamMates = true;
        blaster->itemUserVehicle = this;
        items.push_back(blaster);

        /*NuclearBomb *nuclearBomb = new NuclearBomb();
        nuclearBomb->itemUserVehicle = this;
        items.push_back(nuclearBomb);

        MissileLauncher *missileLauncher = new MissileLauncher();
        missileLauncher->itemUserVehicle = this;
        items.push_back(missileLauncher);

        ReflectorBeam *reflectorBeam = new ReflectorBeam();
        reflectorBeam->itemUserVehicle = this;
        items.push_back(reflectorBeam);

        BouncyBomb *bouncyBomb = new BouncyBomb();
        bouncyBomb->itemUserVehicle = this;
        items.push_back(bouncyBomb);

        Rifle * = new Rifle();
        rifle->itemUserVehicle = this;
        items.push_back(rifle);

        Shotgun *shotgun = new Shotgun();
        shotgun->itemUserVehicle = this;
        items.push_back(shotgun);

        DoomsDay *doomsDay = new DoomsDay();
        doomsDay->itemUserVehicle = this;
        items.push_back(doomsDay);*/

        /*Bolter *bolter = new Bolter();
        bolter->itemUserVehicle = this;
        bolter->name = "Multi Bolter";
        bolter->numBolts = 5;
        bolter->repeatTime_ = 0.25;
        bolter->explosionRadius = 30;
        bolter->manaCostPerUse_ = 100.0 / 16.0;
        bolter->loadingTimeSeconds_ = 10;
        items.push_back(bolter);*/

        /*Railgun *railgun = new Railgun();
        railgun->itemUserVehicle = this;
        items.push_back(railgun);*/

        /*DirtCannon *dirtCannon = new DirtCannon();
        dirtCannon->itemUserVehicle = this;
        dirtCannon->range = 500;
        dirtCannon->deltaAngle = 0.1 * Pi;
        dirtCannon->numWarningProjectiles = 1000;
        dirtCannon->name = "Heavy Melter Cannon";
        items.push_back(dirtCannon);*/

        /*Earthquake *earthquake = new Earthquake();
        earthquake->itemUserVehicle = this;
        items.push_back(earthquake);*/


        Digger *digger = new Digger();
        digger->itemUserVehicle = this;
        itemsSecondary.push_back(digger);

        JetPack *jetPack = new JetPack();
        jetPack->itemUserVehicle = this;
        itemsSecondary.push_back(jetPack);

        /*LandGrower *rockGrower = new LandGrower(true);
        rockGrower->itemUserVehicle = this;
        itemsSecondary.push_back(rockGrower);

        LandGrower *woodGrower = new LandGrower(false);
        woodGrower->itemUserVehicle = this;
        itemsSecondary.push_back(woodGrower);

        Repeller *repeller = new Repeller();
        repeller->itemUserVehicle = this;
        itemsSecondary.push_back(repeller);*/

        /*LaserSight *laserSight = new LaserSight();
        laserSight->itemUserVehicle = this;
        itemsSecondary.push_back(laserSight);*/

        maxHp = 50;
        hp = 50;

        activeItem = 0;


        collisionHandler.init(*this);

        computerControl.init(map, this);

    }

    void updatePixelsVectorIndex() {
        if(direction == Direction::Right) {
            if(running) {
                pixelsVectorIndex = 1 + runningAnimationCounter;
            }
            else {
                pixelsVectorIndex = 0;
            }
        }
        else {
            if(running) {
                pixelsVectorIndex = 1 + numRunningAnimationFrames + 1 + runningAnimationCounter;
            }
            else {
                pixelsVectorIndex = 1 + numRunningAnimationFrames;
            }
        }
    }


    struct CollisionPoint {
        float angle = 0;
        float dx = 0, dy = 0;

    };

    struct CollisionHandler {
        int numCollisionPoints = 32;
        vector<CollisionPoint> collisionPoints;
        sf::RectangleShape rect;

        CollisionPoint collidingPoint;
        bool isCollision = false;

        void renderCollisionPoints(sf::RenderWindow &window, Robot &robot) {
            for(int i=0; i<numCollisionPoints; i++) {
                //rect.setPosition(character.x + 0.5 * character.w * character.scaleX + collisionPoints[i].dx, character.y + 0.5* character.h * character.scaleY + collisionPoints[i].dy); TODO remove this

                rect.setPosition(robot.x + collisionPoints[i].dx, robot.y + collisionPoints[i].dy);

                if(collisionPoints[i].angle >= 0.25 * Pi && collisionPoints[i].angle < 0.75 * Pi) {
                    rect.setFillColor(sf::Color(255, 0, 0, 255));
                }
                if(collisionPoints[i].angle >= 0.75 * Pi && collisionPoints[i].angle < 1.25 * Pi) {
                    rect.setFillColor(sf::Color(255, 255, 0, 255));
                }
                if(collisionPoints[i].angle >= 1.25 * Pi && collisionPoints[i].angle < 1.75 * Pi) {
                    rect.setFillColor(sf::Color(255, 0, 255, 255));
                }
                if((collisionPoints[i].angle >= 1.75 * Pi && collisionPoints[i].angle < 2.0 * Pi) || (collisionPoints[i].angle >= 0.0 * Pi &&
                collisionPoints[i].angle < 0.25 * Pi)) {
                    rect.setFillColor(sf::Color(0, 255, 0, 255));
                }

                window.draw(rect);
            }
        }

        void init(Robot &robot) {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(robot.scaleX, robot.scaleY);
            rect.setOrigin(robot.scaleX*0.5, robot.scaleY*0.5);


            float initialAngle = 0.5*Pi;
            float deltaAngle = 2.0*Pi / numCollisionPoints;

            float radiusX = robot.w*robot.scaleX * 0.5;
            float radiusY = robot.h*robot.scaleY * 0.5;

            for(int i=0; i<numCollisionPoints; i++) {
                CollisionPoint collisionPoint;
                collisionPoint.angle = fmod(initialAngle + i * deltaAngle, 2.0*Pi);
                collisionPoint.dx = radiusX * cos(collisionPoint.angle);
                collisionPoint.dy = radiusY * sin(collisionPoint.angle);
                collisionPoints.push_back(collisionPoint);
            }
        }

        void handleCollisions(Robot &robot, Map &map) {
            isCollision = false;

            for(int p=0; p<numCollisionPoints; p++) {
                //int i = 0;
                int i = p % numCollisionPoints;
                int j = (i + numCollisionPoints/2) % numCollisionPoints;
                /*float x = character.x + character.w*character.scaleX*0.5;
                float y = character.y + character.h*character.scaleY*0.5;
                //TODO remove this */

                float x = robot.x;
                float y = robot.y;

                float dx = cos(collisionPoints[i].angle);
                float dy = sin(collisionPoints[i].angle);

                bool iCollides = false, jCollides = false;
                bool buriedInGround = false;
                float tix = 0, tiy = 0;
                float tjx = 0, tjy = 0;


                iCollides = map.checkCollision(x + collisionPoints[i].dx, y + collisionPoints[i].dy);

                //if(iCollides && fabs(collisionPoints[i].dx) >= character.w*character.scaleX*0.1) {
                if(iCollides && collisionPoints[i].dy <= robot.h*robot.scaleY*0.30) {
                    // TODO check this!
                    if(!isCollision) {
                        collidingPoint = collisionPoints[i];
                    }
                    isCollision = true;
                }

                jCollides = map.checkCollision(x + collisionPoints[j].dx, y + collisionPoints[j].dy);
                int k = 0;
                while(iCollides) {
                    tix = -k * dx;
                    tiy = -k * dy;
                    tjx = -k * dx;
                    tjy = -k * dy;
                    iCollides = map.checkCollision(x + collisionPoints[i].dx + tix, y + collisionPoints[i].dy + tiy);
                    jCollides = map.checkCollision(x + collisionPoints[j].dx + tjx, y + collisionPoints[j].dy + tjy);
                    if(iCollides && jCollides) {
                        buriedInGround = true;
                        break;
                    }
                    if(!iCollides && !jCollides) {
                        break;
                    }
                    k++;
                }
                if(buriedInGround) {
                    //printf("buriedInGround i = %d\n", i);
                    //break;
                }
                if(!iCollides && !jCollides) {
                    //break;
                }
                struct P {
                    int k = 0;
                    int dx = 0;
                    int dy = 0;
                    float getMagnitude() {
                        return sqrt(dx*dx + dy*dy);
                    }
                };
                if(buriedInGround && i == 0) {
                    robot.vy = 0;
                    //break;
                    P pUp, pDown, pLeft, pRight;
                    bool upCollides = true;
                    bool downCollides = true;
                    bool leftCollides = true;
                    bool rightCollides = true;
                    while(upCollides) {
                        pUp.dx = 0;
                        pUp.dy = -pUp.k;
                        upCollides = map.checkCollision(x + pUp.dx, y + pUp.dy);
                        pUp.k++;
                        if(pUp.k > 500) break;
                    }
                    //printf("pUp.k %d\n", pUp.k);

                    while(downCollides) {
                        pDown.dx = 0;
                        pDown.dy = pDown.k;
                        downCollides = map.checkCollision(x + pDown.dx, y + pDown.dy);
                        pDown.k++;
                        if(pDown.k > 500) break;
                    }
                    //printf("pDown.k %d\n", pDown.k);

                    while(leftCollides) {
                        pLeft.dx = -pLeft.k;
                        pLeft.dy = 0;
                        leftCollides = map.checkCollision(x + pLeft.dx, y + pLeft.dy);
                        pLeft.k++;
                        if(pLeft.k > 500) break;
                    }
                    //printf("pLeft.k %d\n", pLeft.k);

                    while(rightCollides) {
                        pRight.dx = pRight.k;
                        pRight.dy = 0;
                        rightCollides = map.checkCollision(x + pRight.dx, y + pRight.dy);
                        pRight.k++;
                        if(pRight.k > 500) break;
                    }
                    //printf("pRight.k %d\n", pRight.k);

                    if(pLeft.getMagnitude() <= pUp.getMagnitude() && pLeft.getMagnitude() <= pDown.getMagnitude() && pLeft.getMagnitude() <= pRight.getMagnitude()) {
                        tix = pLeft.dx;
                        tiy = pLeft.dy;
                    }
                    else if(pRight.getMagnitude() <= pUp.getMagnitude() && pRight.getMagnitude() <= pDown.getMagnitude() && pRight.getMagnitude() <= pLeft.getMagnitude()) {
                        tix = pRight.dx;
                        tiy = pRight.dy;
                    }
                    else if(pDown.getMagnitude() <= pUp.getMagnitude()) {
                        tix = pDown.dx;
                        tiy = pDown.dy;
                    }
                    else {
                        tix = pUp.dx;
                        tiy = pUp.dy;
                    }
                    robot.x += tix;
                    robot.y += tiy;

                    //break;
                }
                else {
                    //character.x += tix;
                    //character.y += tiy;

                    if(fabs(tix) >= 8) {
                        robot.x += tix;
                    }
                    else if(fabs(tiy) < 0.001) {
                        robot.x += tix;
                    }
                    if(fabs(tiy) >= 8) {
                        robot.y += tiy;
                    }
                    else if(fabs(tix) < 0.001) {
                        robot.y += tiy;
                    }
                    /*if(fabs(tix) < 3 && fabs(tiy) < 0.001) {
                        character.x += tix;
                    }
                    if(fabs(tiy) <= 3  && fabs(tiy) < 0.001) {
                        character.y += tiy;
                    }*/
                    if(tiy < 0 && fabs(tix) < 0.001) {
                        robot.readyToJump = true;
                        robot.vy = 0;
                    }
                    if(tiy > 0 && fabs(tix) < 0.001) {
                        robot.vy = 0;
                    }
                }
            }

            // TODO fix the hack below:
            if(robot.x - robot.w * map.scaleX * 0.5 < 0) {
                robot.x = robot.w * map.scaleX * 0.5;
            }
            if(robot.y - robot.h * map.scaleY * 0.5 < 0) {
                robot.y = robot.h * map.scaleY * 0.5;
            }
            if(robot.x + robot.w * map.scaleX * 0.5 > map.w * map.scaleX) {
                robot.x = map.w * map.scaleX - robot.w * map.scaleX * 0.5;
            }
            if(robot.y + robot.h * map.scaleY * 0.5 > map.h * map.scaleY) {
                robot.y = map.h * map.scaleY - robot.h * map.scaleY * 0.5;
            }
        }
    };

    CollisionHandler collisionHandler;
















    void update(float dt, int screenW, int screenH, Map &map) {
        dt = dt > 1.0/60.0 ? 1.0/60.0 : dt;



        centerX = x;
        centerY = y;    // TODO remove this


        computerControl.update(dt);
        globalAngle = direction == Direction::Right ? aimAngle : Pi - aimAngle;


        itemRepeatTimer += dt;

        if(itemUsing == 1 && itemRepeatTimer >= items[activeItem]->repeatTime() && activeItem >= 0 && activeItem < items.size()) {
            float a = direction == Direction::Right ? aimAngle : Pi - aimAngle;
            if(items[activeItem]->noManaCostPerUse()) {
                items[activeItem]->use(x, y, vx, vy, a);
                itemRepeatTimer = 0;
            }
            else {
                if(items[activeItem]->itemMana >= items[activeItem]->manaCostPerUse()) {
                    items[activeItem]->use(x, y, vx, vy, a);
                    items[activeItem]->itemMana -= items[activeItem]->manaCostPerUse();
                    itemRepeatTimer = 0;
                }
            }
            if(usingItemOnce) {
                usingItemOnce = false;
                itemUsing = 0;
            }
        }

        itemSecondaryRepeatTimer += dt;

        if(itemUsingSecondary == 1 && itemSecondaryRepeatTimer >= itemsSecondary[activeItemSecondary]->repeatTime() && activeItemSecondary >= 0 && activeItemSecondary < itemsSecondary.size()) {
            float a = direction == Direction::Right ? aimAngle : Pi - aimAngle;
            if(itemsSecondary[activeItemSecondary]->noManaCostPerUse()) {
                itemsSecondary[activeItemSecondary]->use(x, y, vx, vy, a);
                itemSecondaryRepeatTimer = 0;
            }
            else {
                if(itemsSecondary[activeItemSecondary]->itemMana >= itemsSecondary[activeItemSecondary]->manaCostPerUse()) {
                    itemsSecondary[activeItemSecondary]->use(x, y, vx, vy, a);
                    itemsSecondary[activeItemSecondary]->itemMana -= itemsSecondary[activeItemSecondary]->manaCostPerUse();
                    itemSecondaryRepeatTimer = 0;
                }
            }
        }

        items[activeItem]->update(map, dt);
        itemsSecondary[activeItemSecondary]->update(map, dt);

        for(int i=0; i<items.size(); i++) {
            if(i == activeItem) continue;
            items[i]->updateNonActive(map, dt);
        }
        for(int i=0; i<itemsSecondary.size(); i++) {
            if(i == activeItemSecondary) continue;
            itemsSecondary[i]->updateNonActive(map, dt);
        }

        for(int i=0; i<items.size(); i++) {
            items[i]->loadMana(map, dt);
        }
        for(int i=0; i<itemsSecondary.size(); i++) {
            itemsSecondary[i]->loadMana(map, dt);
        }

        items[activeItem]->itemMana -= items[activeItem]->manaCostPerSecond() * dt;


        if(aiming == 1) {
            aimAngle -= dt*Pi * 0.5;
            if(aimAngle <= -Pi*0.5) {
                aimAngle = -Pi*0.5;
            }
        }
        if(aiming == 2) {
            aimAngle += dt*Pi * 0.5;
            if(aimAngle >= Pi*0.5) {
                aimAngle = Pi*0.5;
            }
        }
        globalAngle = direction == Direction::Right ? aimAngle : Pi - aimAngle;


        float ax = 0;
        float ay = 0;
        ay += gravity;

        vx += ax*dt;
        vy += ay*dt;

        x += vx * dt * speedFactor;
        y += vy * dt * speedFactor;

        speedFactor = 1.0; // TODO fix this hack



        collisionHandler.handleCollisions(*this, map);


        afterUpdate();

        float tx = x + (direction == Direction::Right ? (-w*scaleX*0.5+2*scaleX) : (w*scaleX*0.5-4*scaleX));
        float ty = y - h*scaleY*0.5 - 2;

        for(int i=0; i<1; i++) {
            createSmoke(tx, ty, map.characters, 20, 10, 100, 1);
        }
        /*if(renderEnterHalo) {
            for(int i=0; i<20; i++) {
                EnterVehicleProjectile *evp = new EnterVehicleProjectile();
                float angle = randf2(0, 2.0*Pi);
                float radius = randf2(2, 2.2) * max(w, h);
                evp->x = x + radius * cos(angle);
                evp->y = y + radius * sin(angle);
                if(i % 2 == 0) {
                    evp->vx = randf2(-1, 1);
                    evp->vy = randf2(-1, 1);
                }
                else {
                    evp->vx = -cos(angle) * 50;
                    evp->vy = -sin(angle) * 50;
                }
                evp->gravityHasAnEffect = false;
                evp->duration = randf2(1, 5);
                evp->color = sf::Color(24, 143, 255, randi(30, 255));
                evp->color2 = sf::Color(0, 0, 0, 0);
                evp->characters = map.characters;
                evp->projectileUserVehicle = this;
                projectiles.push_back(evp);
            }
        }*/

        /*if(doubleClickedItemChange) {
            exitThisVehicle();
        }

        if(doubleClickedItemChangeTimer >= 0) {
            doubleClickedItemChangeTimer += dt;
        }
        if(doubleClickedItemChangeActive > 0) {
            doubleClickedItemChangeActive--;
            if(doubleClickedItemChangeActive == 0) {
                doubleClickedItemChange = false;
            }
        }*/
    }



    void afterUpdate(bool forceInactive = false) {
        if(activeItem >= 0 && activeItem < items.size()) {
            items[activeItem]->afterUpdate(forceInactive);
        }
        if(activeItemSecondary >= 0 && activeItemSecondary < itemsSecondary.size()) {
            itemsSecondary[activeItemSecondary]->afterUpdate(forceInactive);
        }
    }


    /*bool respawning = false;
    float respawnTimer = 0;
    float respawnDuration = 5.0;
    float respawnX = 0;
    float respawnY = 0;
    float pointOfDeathX = 0;
    float pointOfDeathY = 0;*/

    void takeDamage(float amount);

    void render(sf::RenderWindow &window);

    /*void renderCrosshair(sf::RenderWindow &window) {
        float dir = direction == Right ? 1 : -1;
        float cx = x;// + w/2;
        float cy = y;// + h/2;
        float cr = 25*scaleX;
        float hx = cx + dir * cos(aimAngle)*cr;
        float hy = cy + sin(aimAngle)*cr; //TODO remove this

        crosshairSprite.setPosition(hx, hy);
        crosshairSprite.setScale(scaleX, scaleY);
        window.draw(crosshairSprite);
    }*/

    bool getCollisionNormal(float px, float py, float &nx, float &ny) {
        float ax = x - w*scaleX * 0.5;
        float ay = y - h*scaleY * 0.5;
        float bx = x + w*scaleX * 0.5;
        float by = y + h*scaleY * 0.5;

        if(!isWithinRect(px, py, ax, ay, bx, by)) {
            return false;
        }

        float dx = px - x;
        float dy = py - y;
        if(dx == 0 && dy == 0) {
            nx = 0;
            ny = 0;
        }
        else {
            float d = sqrt(dx*dx + dy*dy);
            nx = dx / d;
            ny = dy / d;
        }

        return true;
    }

    bool getCollisionReflection(float px, float py, float dx, float dy, float &rx, float & ry) {
        float nx = 0, ny = 0;
        bool collided = getCollisionNormal(px, py, nx, ny);
        if(collided) {
            //float d = sqrt(vx*vx + vy*vy);
            reflectVector(dx, dy, nx, ny, rx, ry);
        }
        return collided;
    }




    bool checkPixelPixelCollision(float px, float py, int &ix, int &iy) {
        ix = px - x + w*scaleX*0.5;
        ix = ix / scaleX;
        iy = py - y + h*scaleY*0.5;
        iy = iy / scaleY + spritePositionDeltaY;

        if(ix < 0) {
            return false;
        }
        if(iy < 0) {
            return false;
        }
        if(ix >= w) {
            return false;
        }
        if(iy >= h) {
            return false;
        }

        // TODO check the animation index
        if(pixelsVector.size() <= pixelsVectorIndex) {
            printf("At Robot::checkPixelPixelCollision(). This vector should be prepared in setup()...\n");
        }
        else if(pixelsVector[pixelsVectorIndex][ix*4 + iy*w*4 + 3] > 200) {              
            /*if(running) {
                vehicleImageRunning[direction][runningAnimationCounter].setPixel(ix, iy, sf::Color(255, 0, 0, 255));
                vehicleTextureRunning[direction][runningAnimationCounter].loadFromImage(vehicleImageRunning[direction][runningAnimationCounter]);
            }
            else {    
                vehicleImageStanding[direction].setPixel(ix, iy, sf::Color(255, 0, 0, 255));
                vehicleTextureStanding[direction].loadFromImage(vehicleImageStanding[direction]);
            }*/
            return true;
        }

        return false;
    }


    bool checkCirclePixelCollision(float px, float py, float r) {
        if(pixelsVector.size() <= pixelsVectorIndex) {
            printf("At Robot::checkCirclePixelCollision(). This vector should be prepared in setup()...\n");
             return false;
        }

        int ix = px - x + w*scaleX*0.5;
        ix = ix / scaleX;
        int iy = py - y + h*scaleY*0.5;
        iy = iy / scaleY + spritePositionDeltaY;

        r = r/scaleX;

        if(ix < -r) {
            return false;
        }
        if(iy < -r) {
            return false;
        }
        if(ix >= w+r) {
            return false;
        }
        if(iy >= h+r) {
            return false;
        }

        int rr = r*r;
        bool hit = false;


        for(int tx=0; tx<w; tx++) {
            for(int ty=0; ty<h; ty++) {
                if(pixelsVector[pixelsVectorIndex][tx*4 + ty*w*4 + 3] > 200) {
                    int dx = tx - ix;
                    int dy = ty - iy;
                    int dd = dx*dx + dy*dy;
                    if(dd <= rr) {
                        hit = true;
                        return true;
                        /*if(running) {
                            vehicleImageRunning[direction][runningAnimationCounter].setPixel(tx, ty, sf::Color(255, 0, 0, 255));
                            vehicleTextureRunning[direction][runningAnimationCounter].loadFromImage(vehicleImageRunning[direction][runningAnimationCounter]);
                        }
                        else {    
                            vehicleImageStanding[direction].setPixel(tx, ty, sf::Color(255, 0, 0, 255));
                            vehicleTextureStanding[direction].loadFromImage(vehicleImageStanding[direction]);
                        }*/
                    }
                }
            }
        }
        return hit;
    }






    void enterThisVehicle(const std::vector<Character*> &characters) {}

    void exitThisVehicle() {}
};

sf::SoundBuffer Robot::robotExplosionSoundBuffer;
bool Robot::initialized = false;














































struct Character {

    struct ComputerControl {
        struct CheckPoint {
            float x = 0, y = 0;
            float radius = 50;
        };

        enum class ControlState { Free, GoingToCheckPoint, ShootingOpponent, LoadingWeapon, GrowingLand, GoingToCheckPointAndDroppingNuclearBomb, AttackingOpponent, AttackingOpponentAndShooting, UsingMelterCannon, UsingEarthquake, GoingToCheckPointWhichIsVehicle, SpawningWalker, SpawningRobots };
        ControlState controlState = ControlState::Free;

        enum class ControlStateSecondary { Free, Digging, UsingJetPack };
        ControlStateSecondary controlStateSecondary = ControlStateSecondary::Free;



        std::vector<std::string> controlStateNames = { "Free", "GoingToCheckPoint", "ShootingOpponent", "LoadingWeapon", "GrowingLand", "GoingToCheckPointAndDroppingNuclearBomb", "AttackingOpponent", "AttackingOpponentAndShooting", "UsingMelterCannon", "UsingEarthquake", "GoingToCheckPointWhichIsVehicle", "SpawningWalker", "SpawningRobots" };

        std::vector<std::string> controlStateSecondaryNames = { "Free", "Digging", "UsingJetPack" };

        void printStatus() {
            printf("Control state: %s, Control state secondary: %s\n", controlStateNames[(int)controlState].c_str(), controlStateSecondaryNames[(int)controlStateSecondary].c_str());
            printf("spawnRobotsTimer %f, secondsToSpawnRobots %f\n", spawnRobotsTimer, secondsToSpawnRobots);
        }


        vector<Character*> otherCharacters;
        Character *thisCharacter = nullptr;
        Character *visibleOpponent = nullptr;
        Vehicle *visibleHostileVehicle = nullptr;
        float opponentLastSeenX = 0, opponentLastSeenY = 0;
        float opponentLastSeenTimer = 0;

        float probabilityOfStartingAnAttack = 1.0 / 5.0;
        //float probabilityOfStartingAnAttack = 1.0;

        Map *map = nullptr;

        vector<CheckPoint> checkPoints;

        bool isActive = false;

        float opponentVisibilityCheckTimer = 0;
        float opponentVisibilityCheckDuration = 0.1;

        float landGrowerCheckTimer = 0;
        float landGrowerCheckDuration = 1;
        float landGrowerSpawnTimer = 0;
        float landGrowerSpawnDuration = 7;

        float checkPointCheckTimer = 0;
        float checkPointCheckDuration = 3;

        float diggingTimer = 0;
        float diggingDuration = 0.1;

        float deltaAimAngle = 0.0666 * Pi;

        float jetPackTimer = 0;
        float jetPackDuration = 0.1;

        float jetPackBombingTimer = 0;
        float jetPackBombingInterval = 2;

        float weaponLoadingTimer = 0;
        float weaponLoadingDuration = 1.5;

        float nuclearBombDroppingTimer = 0;
        float nuclearBombDroppingDuration = 1;
        float nuclearBombDetonationTimer = 0;
        float nuclearBombDetonationDuration = 5;
        //bool nuclearBombDropped = false;

        float melterCannonReactionTime = 0;
        float melterCannonReactionTimeMin = 0.1, melterCannonReactionTimeMax = 1.2;
        float melterCannonDuration = 1.6;
        float melterCannonTimer = 0;
        ControlState controlStateBeforeMelterCannon = ControlState::Free;


        float earthquakeTimer = 0;
        float secondsToEarthquake = 0;

        float spawnWalkerTimer = 0;
        float secondsToSpawnWalker = 0;

        float spawnRobotsTimer = 0;
        float secondsToSpawnRobots = 0;


        void init(Map *map, Character *thisCharacter, vector<Character*> characters) {
            this->map = map;
            this->thisCharacter = thisCharacter;
            this->otherCharacters = characters;
            for(int i=0; i<otherCharacters.size(); i++) {
                if(otherCharacters[i] == thisCharacter) {
                    otherCharacters.erase(otherCharacters.begin()+i);
                    break;
                }
            }
            secondsToEarthquake = thisCharacter->items[16]->loadingTimeSeconds() * randf2(1.0, 2.0);

            secondsToSpawnWalker = thisCharacter->items[17]->loadingTimeSeconds() * randf2(1.0, 2.0);

            secondsToSpawnRobots = thisCharacter->items[18]->loadingTimeSeconds() * randf2(0.11, 1.11);
        }

        void update(float dt) {
            if(!isActive) {
                return;
            }

            if(thisCharacter->items[6]->noManaCostPerUse() && controlState != ControlState::GoingToCheckPointAndDroppingNuclearBomb) {
                controlState = ControlState::GoingToCheckPointAndDroppingNuclearBomb;
            }
            if(controlState != ControlState::GoingToCheckPointAndDroppingNuclearBomb && thisCharacter->activeItem == 6) {
                thisCharacter->activeItem = 0;
            }


            if(controlState != ControlState::GoingToCheckPointAndDroppingNuclearBomb &&
            controlState != ControlState::SpawningRobots) {
                int closestFreeVehicleIndex = -1;
                float distSquaredToClosestFreeVehicle = 1e10;

                for(int i=0; i<vehicles.size(); i++) {
                    if(vehicles[i]->driverCharacter == nullptr) {
                        if(dynamic_cast<Robot*>(vehicles[i])) {
                            continue;
                        }
                        float dx = vehicles[i]->x - thisCharacter->x;
                        float dy = vehicles[i]->y - thisCharacter->y;
                        float dd = dx*dx + dy*dy;
                        if(distSquaredToClosestFreeVehicle > dd) {
                            distSquaredToClosestFreeVehicle = dd;
                            closestFreeVehicleIndex = i;
                        }
                    }
                }
                if(closestFreeVehicleIndex >= 0) {
                    controlState = ControlState::GoingToCheckPointWhichIsVehicle;
                    checkPoints.clear();
                    CheckPoint checkPoint;
                    checkPoint.x = vehicles[closestFreeVehicleIndex]->x;
                    checkPoint.y = vehicles[closestFreeVehicleIndex]->y;
                    checkPoint.radius = 50;
                    checkPoints.push_back(checkPoint);
                }
                else if(controlState == ControlState::GoingToCheckPointWhichIsVehicle) {
                    controlState = ControlState::Free;
                }
            }


            earthquakeTimer += dt;

            if(controlState == ControlState::UsingEarthquake) {
               controlState = ControlState::Free;
            }

            if(controlState == ControlState::Free) {
                if(earthquakeTimer >= secondsToEarthquake) {
                    if(thisCharacter->items[16]->itemMana >= thisCharacter->items[16]->manaCostPerUse()) {
                        controlState = ControlState::UsingEarthquake;
                        thisCharacter->activeItem = 16;
                        earthquakeTimer = 0;
                        secondsToEarthquake = thisCharacter->items[16]->loadingTimeSeconds() * randf2(1.0, 2.0);
                        thisCharacter->useItem();
                    }
                }
            }

            
            spawnWalkerTimer += dt;

            if(controlState == ControlState::SpawningWalker) {
               controlState = ControlState::Free;
            }

            if(controlState == ControlState::Free) {
                if(spawnWalkerTimer >= secondsToSpawnWalker) {
                    if(thisCharacter->items[17]->itemMana >= thisCharacter->items[17]->manaCostPerUse()) {
                        controlState = ControlState::SpawningWalker;
                        thisCharacter->activeItem = 17;
                        spawnWalkerTimer = 0;
                        secondsToSpawnWalker = thisCharacter->items[17]->loadingTimeSeconds() * randf2(1.0, 2.0);
                        thisCharacter->useItem();
                    }
                }
            }

            spawnRobotsTimer += dt;

            if(controlState == ControlState::SpawningRobots) {
               if(thisCharacter->items[18]->itemMana < thisCharacter->items[18]->manaCostPerUse()) {
                    controlState = ControlState::Free;
               }
                else {
                    thisCharacter->useItem();   // TODO check this!
                }
            }

            if(controlState == ControlState::Free) {
                if(spawnRobotsTimer >= secondsToSpawnRobots) {
                    //if(thisCharacter->items[18]->itemMana >= thisCharacter->items[18]->manaCostPerUse()) {
                        controlState = ControlState::SpawningRobots;
                        thisCharacter->activeItem = 18;
                        spawnRobotsTimer = 0;
                        secondsToSpawnRobots = thisCharacter->items[18]->loadingTimeSeconds() * randf2(0.11, 1.11);
                        thisCharacter->useItem();
                    //}
                }
            }


            //if(controlState != ControlState::GrowingLand && 
            //   controlState != ControlState::GoingToCheckPointAndDroppingNuclearBomb) {
            if(controlState == ControlState::Free || controlState == ControlState::GoingToCheckPoint) {
                if(thisCharacter->items[15]->itemMana >= thisCharacter->items[15]->manaCostPerUse()) {
                    if(isMelterCannonApplicable() || isMelterCannonApplicableAgainstVehicles()) {
                        thisCharacter->activeItem = 15;
                        //thisCharacter->useItem();
                        controlStateBeforeMelterCannon = controlState;
                        controlState = ControlState::UsingMelterCannon;
                        melterCannonTimer = 0;
                        melterCannonReactionTime = randf2(melterCannonReactionTimeMin, melterCannonReactionTimeMax);
                    }
                }
            }
            if(controlState == ControlState::UsingMelterCannon) {
                melterCannonTimer += dt;
                if(melterCannonTimer >= melterCannonReactionTime) {
                    thisCharacter->useItem();
                }
                if(melterCannonTimer >= melterCannonDuration) {
                    controlState = controlStateBeforeMelterCannon;
                    /*if(checkPoints.size() > 0) {
                        controlState = ControlState::GoingToCheckPoint;
                    }
                    else {
                        controlState = ControlState::Free;
                    }*/
                    thisCharacter->activeItem = 0;
                }
            }
            opponentVisibilityCheckTimer += dt;
            if(opponentVisibilityCheckTimer >= opponentVisibilityCheckDuration) {
                if(controlState != ControlState::GrowingLand &&
                controlState != ControlState::GoingToCheckPointAndDroppingNuclearBomb &&
                controlState != ControlState::UsingMelterCannon &&
                controlState != ControlState::GoingToCheckPointWhichIsVehicle &&
                controlState != ControlState::SpawningRobots) {
                    checkOpponentVisibility();
                }
                opponentVisibilityCheckTimer = 0;
            }

            nuclearBombDroppingTimer += dt;
            if(nuclearBombDroppingTimer >= nuclearBombDroppingDuration && thisCharacter->items[6]->itemMana >= thisCharacter->items[6]->manaCostPerUse() && controlState != ControlState::GoingToCheckPointAndDroppingNuclearBomb &&
            controlState != ControlState::SpawningRobots) {
                float dist = distanceToGroundBelowCharacter();
                if(dist > map->h * map->scaleY * 0.125) {
                    controlState = ControlState::GoingToCheckPointAndDroppingNuclearBomb;
                    thisCharacter->activeItem = 6;
                    thisCharacter->useItemOnce();

                    checkPoints.clear();
                }
                nuclearBombDroppingTimer = 0;
                nuclearBombDetonationTimer = 0;
            }

            if(controlState != ControlState::GrowingLand &&
               controlState != ControlState::GoingToCheckPointWhichIsVehicle &&
               controlState != ControlState::SpawningRobots) {
                landGrowerCheckTimer += dt;
                landGrowerSpawnTimer += dt;
                if(landGrowerCheckTimer >= landGrowerCheckDuration) {
                    landGrowerCheckTimer = 0;
                    landGrowerSpawnTimer = 0;
                    if(map->numOccupiedTiles < 0.02 * map->w * map->h) {
                        controlState = ControlState::GrowingLand;
                        bool rock = randf2(0, 1) < 0.5;
                        bool tile = randf2(0, 1) < 0.5;
                        if(rock && tile) {
                            thisCharacter->activeItemSecondary = 4;
                        }
                        else if(rock && !tile) {
                            thisCharacter->activeItemSecondary = 2;
                        }
                        else if(!rock && tile) {
                            thisCharacter->activeItemSecondary = 5;
                        }
                        else {
                            thisCharacter->activeItemSecondary = 3;
                        }
                    }
                }
            }
            if(controlState == ControlState::GrowingLand) {
                landGrowerSpawnTimer += dt;

                thisCharacter->useItemSecondary();

                if(landGrowerSpawnTimer >= landGrowerSpawnDuration) {
                    controlState = ControlState::Free;
                }
            }
            else {
                thisCharacter->stopUseItemSecondary();  // TODO check this!
            }

            if(controlState == ControlState::ShootingOpponent) {
                thisCharacter->useItem();

                if(thisCharacter->items[thisCharacter->activeItem]->itemMana < thisCharacter->items[thisCharacter->activeItem]->manaCostPerUse()) {

                    if(thisCharacter->activeItem == 1 || thisCharacter->activeItem == 2) {
                        if(thisCharacter->items[thisCharacter->activeItem]->noManaCostPerUse()) {
                            thisCharacter->items[thisCharacter->activeItem]->use(0, 0, 0, 0, 0);
                        }
                    }
                    //changeWeapon();
                    controlState = ControlState::LoadingWeapon;
                    weaponLoadingTimer = 0;
                }
            }
            else if(controlState == ControlState::AttackingOpponentAndShooting) {
                thisCharacter->useItem();

                if(thisCharacter->items[thisCharacter->activeItem]->itemMana < thisCharacter->items[thisCharacter->activeItem]->manaCostPerUse()) {

                    if(thisCharacter->activeItem == 1 || thisCharacter->activeItem == 2) {
                        if(thisCharacter->items[thisCharacter->activeItem]->noManaCostPerUse()) {
                            thisCharacter->items[thisCharacter->activeItem]->use(0, 0, 0, 0, 0);
                        }
                    }

                    changeWeapon();
                    //controlState = ControlState::LoadingWeapon;
                    //weaponLoadingTimer = 0;
                }
            }
            else if(controlState != ControlState::GoingToCheckPointAndDroppingNuclearBomb &&
                    controlState != ControlState::UsingMelterCannon &&
                    controlState != ControlState::UsingEarthquake &&
                    controlState != ControlState::SpawningWalker &&
                    controlState != ControlState::SpawningRobots) {
                thisCharacter->stopUseItem();
            }

            if(controlState == ControlState::LoadingWeapon) {
                weaponLoadingTimer += dt;
                if(weaponLoadingTimer >= weaponLoadingDuration) {
                    changeWeapon();
                    //controlState = ControlState::ShootingOpponent;
                    controlState = ControlState::Free;
                    weaponLoadingTimer = 0;
                }
            }

            if(visibleOpponent == nullptr && visibleHostileVehicle == nullptr) {
                opponentLastSeenTimer += dt;
            }

            if(opponentLastSeenTimer >= 10 && (controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting)) {
                controlState = ControlState::Free;
            }

            if(controlState == ControlState::Free) {
                //printf("Free\n");

                checkPointCheckTimer += dt;

                if(checkPointCheckTimer > checkPointCheckDuration) {
                    if(checkPoints.size() == 0) {
                        CheckPoint checkPoint;
                        checkPoint.x = randf2(50, map->w * map->scaleX - 50);
                        checkPoint.y = randf2(50, map->h * map->scaleY - 50);
                        checkPoints.push_back(checkPoint);
                    }
                    checkPointCheckTimer = 0;
                    controlState = ControlState::GoingToCheckPoint;
                }

                /*if((thisCharacter->activeItem == 3 || thisCharacter->activeItem == 11) && opponentLastSeenTimer < 2.0 && (opponentLastSeenX != 0 && opponentLastSeenY != 0)) {
                    controlState = ControlState::AttackingOpponent;
                }*/ // TODO continue here!
            }

            if(controlState == ControlState::GoingToCheckPointAndDroppingNuclearBomb) {
                if(checkPoints.size() == 0) {
                    CheckPoint checkPoint;
                    checkPoint.x = randf2(50, map->w * map->scaleX - 50);
                    checkPoint.y = randf2(50, 100);
                    checkPoints.push_back(checkPoint);
                }
                nuclearBombDetonationTimer += dt;
                if(nuclearBombDetonationTimer >= nuclearBombDetonationDuration) {
                    //printf("Detonating the nuclear bomb!\n");
                    thisCharacter->activeItem = 6;
                    thisCharacter->useItemOnce();
                    //nuclearBombDropped = false;
                    controlState = ControlState::GoingToCheckPoint;
                }
            }

            if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
                //if(checkPoints.size() == 0) {
                    checkPoints.clear();

                    CheckPoint checkPoint;
                    checkPoint.radius = 150;
                    checkPoint.x = opponentLastSeenX;
                    checkPoint.y = opponentLastSeenY;
                    checkPoints.push_back(checkPoint);
                //}
            }

            thisCharacter->stopMoveLeft();
            thisCharacter->stopMoveRight();

            if((controlState == ControlState::GoingToCheckPoint ||
             controlState == ControlState::GoingToCheckPointAndDroppingNuclearBomb ||
             controlState == ControlState::AttackingOpponent || 
             controlState == ControlState::AttackingOpponentAndShooting ||
             controlState == ControlState::GoingToCheckPointWhichIsVehicle) && checkPoints.size() > 0) {

                bool colliding = thisCharacter->collisionHandler.isCollision;

                CheckPoint checkPoint = checkPoints[0];

                
                float dx = thisCharacter->x - checkPoint.x;
                float dy = thisCharacter->y - checkPoint.y;

                float a = atan2(-dy, -dx);

                if((colliding || (a < 0.52*Pi && a > 0.48*Pi)) && controlStateSecondary != ControlStateSecondary::Digging) {
                    controlStateSecondary = ControlStateSecondary::Digging;
                    diggingTimer = 0;
                    for(int k=0; k<thisCharacter->itemsSecondary.size(); k++) {
                        if(dynamic_cast<Digger*>(thisCharacter->itemsSecondary[k])) {
                            thisCharacter->activeItemSecondary = k;
                            break;
                        }
                    }
                }


                if(controlStateSecondary == ControlStateSecondary::Digging) {
                    diggingTimer += dt;
                    if(diggingTimer >= diggingDuration) {
                        controlStateSecondary = ControlStateSecondary::Free;
                        thisCharacter->stopUseItemSecondary();
                    }
                    else {
                        thisCharacter->useItemSecondary();
                    }
                }


                if(checkPoint.x + 2 < thisCharacter->x) {
                    thisCharacter->moveLeft();
                }
                else if(checkPoint.x - 2 > thisCharacter->x) {
                    thisCharacter->moveRight();
                }
                if(checkPoint.x < thisCharacter->x) {
                    thisCharacter->aimAngle = atan2(-dy, dx);
                }
                else {
                    thisCharacter->aimAngle = atan2(-dy, -dx);
                }
                if(controlState == ControlState::AttackingOpponentAndShooting) {
                    thisCharacter->aimAngle += randf2(-deltaAimAngle, deltaAimAngle);
                }

                if(checkPoint.y < thisCharacter->y) {
                    thisCharacter->jump();
                }
                if(checkPoint.y < thisCharacter->y && controlStateSecondary == ControlStateSecondary::Free) {
                    jetPackTimer = 0;
                    controlStateSecondary = ControlStateSecondary::UsingJetPack;
                    for(int k=0; k<thisCharacter->itemsSecondary.size(); k++) {
                        if(dynamic_cast<JetPack*>(thisCharacter->itemsSecondary[k])) {
                            thisCharacter->activeItemSecondary = k;
                            break;
                        }
                    }
                }



                if(controlStateSecondary == ControlStateSecondary::UsingJetPack) {
                    jetPackTimer += dt;
                    if(jetPackTimer >= jetPackDuration) {
                        controlStateSecondary = ControlStateSecondary::Free;
                        thisCharacter->stopUseItemSecondary();
                    }
                    else {
                        thisCharacter->useItemSecondary();

                        if(controlState != ControlState::GoingToCheckPointAndDroppingNuclearBomb && controlState != ControlState::AttackingOpponent && controlState != ControlState::AttackingOpponentAndShooting) {

                            float d = checkGroundBetweenCharacterAndCheckPoint();

                            jetPackBombingTimer += dt;

                            if(d > 45 * thisCharacter->scaleX && d < 110 * thisCharacter->scaleX && jetPackBombingTimer >= jetPackBombingInterval && fabs(thisCharacter->vy)*thisCharacter->scaleX < Bomb::throwingVelocity) {  // TODO check this!
                                for(int k=0; k<thisCharacter->items.size(); k++) {
                                    if(dynamic_cast<Bomb*>(thisCharacter->items[k])) {
                                        thisCharacter->activeItem = k;
                                        break;
                                    }
                                }
                                jetPackBombingTimer = 0;
                                thisCharacter->useItem();
                            }
                        }
                    }
                }


                float tx = thisCharacter->x - checkPoint.x;
                float ty = thisCharacter->y - checkPoint.y;
                float rr = tx*tx + ty*ty;
                if(rr < checkPoint.radius*checkPoint.radius) {
                    if(controlState == ControlState::GoingToCheckPointWhichIsVehicle) {
                        // TODO Enter the vehicle, fix this mess
                        thisCharacter->doubleClickedItemChange = true;
                        thisCharacter->doubleClickedItemChangeActive = 2;
                        thisCharacter->doubleClickedItemChangeTimer = -1;
                    }
                    checkPoints.clear();
                    controlState = ControlState::Free;
                }
            }
        }

        sf::CircleShape cs;

        void render(sf::RenderWindow &window) {
            for(int i=0; i<checkPoints.size(); i++) {
                cs.setRadius(checkPoints[i].radius);
                cs.setFillColor(sf::Color(255, 0, 0, 40));
                cs.setPosition(sf::Vector2f(checkPoints[i].x - checkPoints[i].radius, checkPoints[i].y - checkPoints[i].radius));
                window.draw(cs);
            }
        }

        void onDeath() {
            if(controlState == ControlState::ShootingOpponent ||
               controlState == ControlState::LoadingWeapon ||
               controlState == ControlState::AttackingOpponent ||
               controlState == ControlState::AttackingOpponentAndShooting ||
               controlState == ControlState::GoingToCheckPointAndDroppingNuclearBomb || 
               controlState == ControlState::UsingMelterCannon)
            {
                if(controlState == ControlState::UsingMelterCannon) {
                    thisCharacter->activeItem = 0;
                }
                controlState = ControlState::Free;

                checkPoints.clear();

                CheckPoint checkPoint;
                checkPoint.x = randf2(50, map->w * map->scaleX - 50);
                checkPoint.y = randf2(50, map->h * map->scaleY - 50);
                checkPoints.push_back(checkPoint);
            }
        }

        void changeWeapon() {
            if(!visibleOpponent && !visibleHostileVehicle) {
                return;
            }

            /*if(thisCharacter->activeItem == 1 || thisCharacter->activeItem == 2) {
                if(thisCharacter->items[thisCharacter->activeItem]->noManaCostPerUse()) {
                    thisCharacter->items[thisCharacter->activeItem]->use(0, 0, 0, 0, 0);
                }
            }*/

            float tx = 0;
            float ty = 0;

            if(visibleHostileVehicle) {
                tx = visibleHostileVehicle->x - thisCharacter->x;
                ty = visibleHostileVehicle->y - thisCharacter->y;
            }
            else {
                tx = visibleOpponent->x - thisCharacter->x;
                ty = visibleOpponent->y - thisCharacter->y;
            }

            float d = sqrt(tx*tx + ty*ty);

            bool weaponReady = false;
            int i = 0;
            while(!weaponReady) {
                i++;
                if(i > 20) break; // TODO check this!

                float rn = randf2(0, 1);
                if(d < 250) {
                    if(rn < 1.0 / 4.0) {
                        for(int k=0; k<thisCharacter->items.size(); k++) {
                            if(dynamic_cast<Shotgun*>(thisCharacter->items[k])) {
                                thisCharacter->activeItem = k;
                                break;
                            }
                        }
                    }
                    else if(rn < 2.0/4.0) {
                        for(int k=0; k<thisCharacter->items.size(); k++) {
                            if(dynamic_cast<FlameThrower*>(thisCharacter->items[k])) {
                                thisCharacter->activeItem = k;
                                break;
                            }
                        }
                    }
                    else if(rn < 3.0/4.0) {
                        for(int k=0; k<thisCharacter->items.size(); k++) {
                            if(dynamic_cast<Blaster*>(thisCharacter->items[k])) {
                                thisCharacter->activeItem = k;
                                break;
                            }
                        }
                    }
                    else {
                        for(int k=0; k<thisCharacter->items.size(); k++) {
                            if(dynamic_cast<Rifle*>(thisCharacter->items[k])) {
                                thisCharacter->activeItem = k;
                                break;
                            }
                        }
                    }
                }
                else if(fabs(tx) < 666 && fabs(ty) < 25) {  // TODO fix this!
                    if(rn < 1.0/4.0) {
                        for(int k=0; k<thisCharacter->items.size(); k++) {
                            if(dynamic_cast<Bomb*>(thisCharacter->items[k])) {
                                thisCharacter->activeItem = k;
                                break;
                            }
                        }
                    }
                    else if(rn < 2.0/4.0) {
                        for(int k=0; k<thisCharacter->items.size(); k++) {
                            if(dynamic_cast<Napalm*>(thisCharacter->items[k])) {
                                thisCharacter->activeItem = k;
                                break;
                            }
                        }
                    }
                    else if(rn < 3.0/4.0) {
                        for(int k=0; k<thisCharacter->items.size(); k++) {
                            if(dynamic_cast<ClusterBomb*>(thisCharacter->items[k])) {
                                thisCharacter->activeItem = k;
                                break;
                            }
                        }
                    }
                    else {
                        for(int k=0; k<thisCharacter->items.size(); k++) {
                            if(dynamic_cast<FireBall*>(thisCharacter->items[k])) {
                                thisCharacter->activeItem = k;
                                break;
                            }
                        }
                    }
                }
                else if(fabs(tx) < 1000 && fabs(ty) < 25 &&
                thisCharacter->items[19]->itemMana >= thisCharacter->items[19]->manaCostPerUse()) {
                    for(int k=0; k<thisCharacter->items.size(); k++) {
                        if(dynamic_cast<FireBall*>(thisCharacter->items[k])) {
                            thisCharacter->activeItem = k;
                            break;
                        }
                    }
                }
                else if(rn < 1.0/7.0) {
                    for(int k=0; k<thisCharacter->items.size(); k++) {
                        if(dynamic_cast<LightningStrike*>(thisCharacter->items[k])) {
                            thisCharacter->activeItem = k;
                            break;
                        }
                    }
                }
                else if(rn < 2.0/7.0) {
                    for(int k=0; k<thisCharacter->items.size(); k++) {
                        if(dynamic_cast<Blaster*>(thisCharacter->items[k])) {
                            thisCharacter->activeItem = k;
                            break;
                        }
                    }
                }
                else if(rn < 3.0/7.0) {
                    for(int k=0; k<thisCharacter->items.size(); k++) {
                        if(dynamic_cast<MissileLauncher*>(thisCharacter->items[k])) {
                            thisCharacter->activeItem = k;
                            break;
                        }
                    }
                }
                else if(rn < 4.0/7.0) {
                    for(int k=0; k<thisCharacter->items.size(); k++) {
                        if(dynamic_cast<ReflectorBeam*>(thisCharacter->items[k])) {
                            thisCharacter->activeItem = k;
                            break;
                        }
                    }
                }
                else if(rn < 5.0/7.0) {
                    for(int k=0; k<thisCharacter->items.size(); k++) {
                        if(dynamic_cast<Rifle*>(thisCharacter->items[k])) {
                            thisCharacter->activeItem = k;
                            break;
                        }
                    }

                }
                else if(rn < 6.0/7.0) {
                    for(int k=0; k<thisCharacter->items.size(); k++) {
                        if(dynamic_cast<Bolter*>(thisCharacter->items[k])) {
                            thisCharacter->activeItem = k;
                            break;
                        }
                    }
                }
                else {
                    for(int k=0; k<thisCharacter->items.size(); k++) {
                        if(dynamic_cast<Railgun*>(thisCharacter->items[k])) {
                            thisCharacter->activeItem = k;
                            break;
                        }
                    }
                }
                weaponReady = thisCharacter->items[thisCharacter->activeItem]->itemMana >= thisCharacter->items[thisCharacter->activeItem]->manaCostPerUse();
            }
        }


        float checkGroundBetweenCharacterAndCheckPoint() {
            if(checkPoints.size() == 0) return -1;

            float tx = checkPoints[0].x - thisCharacter->x;
            float ty = checkPoints[0].y - thisCharacter->y;
            float distanceToCheckPoint = sqrt(tx*tx + ty*ty);

            float dx = tx / distanceToCheckPoint;
            float dy = ty / distanceToCheckPoint;
            for(int k=0; k<5000; k++) {

                float x = thisCharacter->x + dx*k;
                float y = thisCharacter->y + dy*k;

                if(map->checkCollision(x, y)) {
                    float distanceToGround = sqrt(dx*k*dx*k + dy*k*dy*k);
                    if(distanceToCheckPoint < distanceToGround) {
                        return -1;
                    }
                    else {
                        return distanceToGround;
                    }
                }
            }
            return -1;
        }


        float distanceToGroundBelowCharacter() {
            float dy = 1;

            float x = thisCharacter->x;
            float characterY = thisCharacter->y;

            for(int k=0; k<5000; k++) {
                float y = characterY + dy*k;

                if(map->checkCollision(x, y)) {
                    float distanceToGround = sqrt(dy*k*dy*k);
                    return distanceToGround;
                }
            }
            return -1;
        }

        bool isMelterCannonApplicable() {
            for(int i=0; i<otherCharacters.size(); i++) {
                if(otherCharacters[i]->respawning || otherCharacters[i]->inThisVehicle) {
                    continue;
                }
                float tx = otherCharacters[i]->x - thisCharacter->x;
                float ty = otherCharacters[i]->y - thisCharacter->y;
                float d = sqrt(tx*tx + ty*ty);
                if(d == 0) continue;
                float dx = tx / d;
                float dy = ty / d;

                int hitsGround = 0;

                int n = min(round(d), 290);

                for(int i=0; i<n; i++) {
                    if(map->checkCollision(thisCharacter->x + dx * i, thisCharacter->y + dy * i)) {
                        hitsGround++;
                    }
                }
                bool freeTile = false;
                if(d < 300) {
                    freeTile = !map->checkCollision(otherCharacters[i]->x, otherCharacters[i]->y);
                }
                else {
                    freeTile = !map->checkCollision(thisCharacter->x + dx * 300, thisCharacter->y + dy * 300);
                }

                if(hitsGround >= 100 && freeTile) {
                    if(otherCharacters[i]->x < thisCharacter->x) {
                        thisCharacter->direction = Character::Direction::Left;
                        thisCharacter->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }
                    else {
                        thisCharacter->direction = Character::Direction::Right;
                        thisCharacter->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }

                    return true;
                }

            }
            return false;
        }


        bool isMelterCannonApplicableAgainstVehicles() {
            for(int i=0; i<vehicles.size(); i++) {
                if(vehicles[i]->driverCharacter == nullptr || vehicles[i]->driverCharacter == thisCharacter) {
                    continue;
                }
                float tx = vehicles[i]->x - thisCharacter->x;
                float ty = vehicles[i]->y - thisCharacter->y;
                float d = sqrt(tx*tx + ty*ty);
                if(d == 0) continue;
                float dx = tx / d;
                float dy = ty / d;

                int hitsGround = 0;

                int n = min(round(d), 290);

                for(int i=0; i<n; i++) {
                    if(map->checkCollision(thisCharacter->x + dx * i, thisCharacter->y + dy * i)) {
                        hitsGround++;
                    }
                }
                bool freeTile = false;
                if(d < 300) {
                    freeTile = !map->checkCollision(vehicles[i]->x, vehicles[i]->y);
                }
                else {
                    freeTile = !map->checkCollision(thisCharacter->x + dx * 300, thisCharacter->y + dy * 300);
                }

                if(hitsGround >= 100 && freeTile) {
                    if(vehicles[i]->x < thisCharacter->x) {
                        thisCharacter->direction = Character::Direction::Left;
                        thisCharacter->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }
                    else {
                        thisCharacter->direction = Character::Direction::Right;
                        thisCharacter->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }

                    return true;
                }

            }
            return false;
        }









        void checkOpponentVisibility() {
            if(controlState == ControlState::LoadingWeapon) {
                return;
            }

            visibleOpponent = nullptr;
            bool opponentVisible = false;

            for(int i=0; i<otherCharacters.size(); i++) {
                if(otherCharacters[i]->respawning || otherCharacters[i]->inThisVehicle) {
                    continue;
                }
                float tx = otherCharacters[i]->x - thisCharacter->x;
                float ty = otherCharacters[i]->y - thisCharacter->y;
                float d = sqrt(tx*tx + ty*ty);
                float dx = tx / d;
                float dy = ty / d;
                float rr = 500;
                for(int k=0; k<5000; k++) {
                    float x = thisCharacter->x + dx*k;
                    float y = thisCharacter->y + dy*k;

                    if(map->checkCollision(x, y)) {
                        break;
                    }

                    float vx = otherCharacters[i]->x - x;
                    float vy = otherCharacters[i]->y - y;

                    if(vx*vx + vy*vy < rr) {
                        visibleOpponent = otherCharacters[i];
                        opponentVisible = true;
                        opponentLastSeenX = otherCharacters[i]->x;
                        opponentLastSeenY = otherCharacters[i]->y;
                        opponentLastSeenTimer = 0;

                        if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
                            controlState = ControlState::AttackingOpponentAndShooting;
                        }
                        else {
                            controlState = ControlState::ShootingOpponent;
                        }

                        if(visibleOpponent->x < thisCharacter->x) {
                            thisCharacter->direction = Character::Direction::Left;
                            if((thisCharacter->activeItem == 0 || 
                               thisCharacter->activeItem == 1 || 
                               thisCharacter->activeItem == 2) &&
                               fabs(ty) < 50 &&
                               fabs(tx) < 666.0) {
                                thisCharacter->aimAngle = -0.5 * asin(fabs(tx)*gravity/(1000*1000)) + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                            else {
                                thisCharacter->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                        }
                        else {
                            thisCharacter->direction = Character::Direction::Right;
                            if((thisCharacter->activeItem == 0 || 
                               thisCharacter->activeItem == 1 || 
                               thisCharacter->activeItem == 2) &&
                               fabs(ty) < 50 &&
                               fabs(tx) < 666.0) {
                                thisCharacter->aimAngle = -0.5 * asin(fabs(tx)*gravity/(1000*1000)) + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                            else {
                                thisCharacter->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                        }

                        break;
                    }
                }
                if(opponentVisible) {
                    break;
                }
            }




            bool hostileVehicleVisible = false;
            visibleHostileVehicle = nullptr;

            if(!opponentVisible) {

                for(int i=0; i<vehicles.size(); i++) {
                    /*if(vehicles[i]->driverCharacter == nullptr || vehicles[i]->driverCharacter == thisCharacter) {
                        if(!dynamic_cast<Robot*>(vehicles[i])) {
                            continue;
                        }
                    }*/
                    if((vehicles[i]->driverCharacter == nullptr && dynamic_cast<Walker*>(vehicles[i])) || vehicles[i]->driverCharacter == thisCharacter) {
                        continue;
                    }
                    float tx = vehicles[i]->x - thisCharacter->x;
                    float ty = vehicles[i]->y - thisCharacter->y;
                    float d = sqrt(tx*tx + ty*ty);
                    float dx = tx / d;
                    float dy = ty / d;
                    float rr = 500;
                    for(int k=0; k<5000; k++) {
                        float x = thisCharacter->x + dx*k;
                        float y = thisCharacter->y + dy*k;

                        if(map->checkCollision(x, y)) {
                            break;
                        }

                        float vx = vehicles[i]->x - x;
                        float vy = vehicles[i]->y - y;

                        if(vx*vx + vy*vy < rr) {
                            visibleHostileVehicle = vehicles[i];
                            hostileVehicleVisible = true;
                            opponentLastSeenX = vehicles[i]->x;
                            opponentLastSeenY = vehicles[i]->y;
                            opponentLastSeenTimer = 0;

                            if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
                                controlState = ControlState::AttackingOpponentAndShooting;
                            }
                            else {
                                controlState = ControlState::ShootingOpponent;
                            }

                            if(visibleHostileVehicle->x < thisCharacter->x) {
                                thisCharacter->direction = Character::Direction::Left;

                                if((thisCharacter->activeItem == 0 || 
                                    thisCharacter->activeItem == 1 || 
                                    thisCharacter->activeItem == 2) &&
                                    fabs(ty) < 50 &&
                                    fabs(tx) < 666.0) {
                                    thisCharacter->aimAngle = -0.5 * asin(fabs(tx)*gravity/(1000*1000)) + randf2(-deltaAimAngle, deltaAimAngle);
                                }
                                else {
                                    thisCharacter->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
                                }
                            }
                            else {
                                thisCharacter->direction = Character::Direction::Right;

                                if((thisCharacter->activeItem == 0 || 
                                    thisCharacter->activeItem == 1 || 
                                    thisCharacter->activeItem == 2) &&
                                    fabs(ty) < 50 &&
                                    fabs(tx) < 666.0) {
                                    thisCharacter->aimAngle = -0.5 * asin(fabs(tx)*gravity/(1000*1000)) + randf2(-deltaAimAngle, deltaAimAngle);
                                }
                                else {
                                    thisCharacter->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
                                }
                            }

                            break;
                        }
                    }
                    if(hostileVehicleVisible) {
                        break;
                    }
                }

            }





            if(!opponentVisible && !hostileVehicleVisible && controlState == ControlState::ShootingOpponent) {
                if(randf2(0, 1) < probabilityOfStartingAnAttack) {
                    checkPoints.clear();

                    CheckPoint checkPoint;
                    checkPoint.radius = 150;
                    checkPoint.x = opponentLastSeenX + randf2(-1, 1);
                    checkPoint.y = opponentLastSeenY + randf2(-1, 1);
                    checkPoints.push_back(checkPoint);

                    controlState = ControlState::AttackingOpponent;
                }
                else {
                    checkPoints.clear();
                    CheckPoint checkPoint;
                    checkPoint.x = randf2(50, map->w * map->scaleX - 50);
                    checkPoint.y = randf2(50, map->h * map->scaleY - 50);
                    checkPoints.push_back(checkPoint);

                    controlState = ControlState::Free;
                }
            }

            if(!opponentVisible && !hostileVehicleVisible && controlState == ControlState::AttackingOpponentAndShooting) {
                controlState = ControlState::AttackingOpponent;
            }

        }

    };

    ComputerControl computerControl;

    float x = 0, y = 0;
    float vx = 0, vy = 0;
    float speed = 75;
    float m = 1;
    int w = 1, h = 1;
    bool readyToJump = false;
    int scaleX = 1;
    int scaleY = 1;

    ColorTheme colorTheme;

    BoundingBox boundingBox;

    float centerX = 0, centerY = 0; // TODO remove these

    float aimAngle = 0;
    float globalAngle = 0;

    float maxHp = 100;
    float hp = 100;

    float maxMana = 100;
    //float mana = 100;

    Vehicle *inThisVehicle = nullptr;


    enum Direction { Left, Right };
    Direction direction = Left;

    sf::Image characterImageStanding;
    sf::Texture characterTextureStanding;
    sf::Sprite characterSpriteStanding;

    std::vector<sf::Image> characterImageRunning;
    std::vector<sf::Texture> characterTextureRunning;
    std::vector<sf::Sprite> characterSpriteRunning;


    sf::Image crosshairImage;
    sf::Texture crosshairTexture;
    sf::Sprite crosshairSprite;

    vector<Item*> items;
    int activeItem = 0;

    vector<Item*> itemsSecondary;
    int activeItemSecondary = 0;

    Map *map = nullptr;

    const int numRunningAnimationFrames = 8;
    int runningAnimationCounter = 0;
    long runningAnimationCounterX = 0;

    bool doubleClickedItemChange = false;
    float doubleClickedItemChangeTimer = -1;
    float doubleClickDurationSeconds = 0.25;
    int doubleClickedItemChangeActive = 0;

    void setup(Map *map, const string &filenameBase, int screenW, int screenH, int scaleX, int scaleY, ColorTheme colorTheme) {
        this->map = map;

        this->colorTheme = colorTheme;

        std::string fileName = "data/textures/" + filenameBase+"_standing.png";
        if(!characterImageStanding.loadFromFile(fileName)) {
            printf("Couldn't open file '%s'!\n", fileName.c_str());
            return;
        }
        /*if(!characterImage.loadFromFile(filename)) {
            printf("Couldn't open file '%s'!\n", filename.c_str());
            return;
        }*/
        sf::Vector2u size = characterImageStanding.getSize();
        w = size.x;
        h = size.y;

        characterTextureStanding.loadFromImage(characterImageStanding);
        characterSpriteStanding.setTexture(characterTextureStanding, true);
        characterSpriteStanding.setOrigin(w*0.5, h*0.5 - 1);




        characterImageRunning.resize(numRunningAnimationFrames);
        characterTextureRunning.resize(numRunningAnimationFrames);
        characterSpriteRunning.resize(numRunningAnimationFrames);

        for(int i=0; i<numRunningAnimationFrames; i++) {
            std::string fileName = "data/textures/" + filenameBase + "_running_" + std::to_string(i+1) + "of"+std::to_string(numRunningAnimationFrames)+".png";(numRunningAnimationFrames)+".png";
            //std::string fileName = "data/textures/" + filenameBase + "_walking_" + std::to_string(i+1) + "of"+std::to_string(numRunningAnimationFrames)+".png";(numRunningAnimationFrames)+".png";
            if(!characterImageRunning[i].loadFromFile(fileName)) {
                printf("Couldn't open file '%s'!\n", fileName.c_str());
                return;
            }
            characterTextureRunning[i].loadFromImage(characterImageRunning[i]);
            characterSpriteRunning[i].setTexture(characterTextureRunning[i], true);
            characterSpriteRunning[i].setOrigin(w*0.5, h*0.5 - 1);
        }

        x = randf(w, screenW-w-1);
        y = screenH/2;
        this->scaleX = scaleX;
        this->scaleY = scaleY;


        if(!crosshairImage.loadFromFile("data/textures/crosshair.png")) {
            printf("Couldn't open file 'data/textures/crosshair.png'!\n");
            return;
        }
        crosshairTexture.loadFromImage(crosshairImage);
        crosshairSprite.setTexture(crosshairTexture, true);

        sf::Vector2u crosshairSize = crosshairImage.getSize();
        crosshairSprite.setOrigin(crosshairSize.x*0.5, crosshairSize.y*0.5);

        Bomb *bomb = new Bomb();
        bomb->itemUserCharacter = this; // TODO fix
        items.push_back(bomb);

        ClusterBomb *clusterBomb = new ClusterBomb();
        clusterBomb->itemUserCharacter = this;
        items.push_back(clusterBomb);

        Napalm *napalm = new Napalm();
        napalm->itemUserCharacter = this;
        items.push_back(napalm);

        FlameThrower *flameThrower = new FlameThrower();
        flameThrower->itemUserCharacter = this;
        items.push_back(flameThrower);

        LightningStrike *lightningStrike = new LightningStrike();
        lightningStrike->itemUserCharacter = this;
        items.push_back(lightningStrike);

        Blaster *blaster = new Blaster();
        blaster->itemUserCharacter = this;
        items.push_back(blaster);

        NuclearBomb *nuclearBomb = new NuclearBomb();
        nuclearBomb->itemUserCharacter = this;
        items.push_back(nuclearBomb);

        MissileLauncher *missileLauncher = new MissileLauncher();
        missileLauncher->itemUserCharacter = this;
        items.push_back(missileLauncher);

        ReflectorBeam *reflectorBeam = new ReflectorBeam();
        reflectorBeam->itemUserCharacter = this;
        items.push_back(reflectorBeam);

        BouncyBomb *bouncyBomb = new BouncyBomb();
        bouncyBomb->itemUserCharacter = this;
        items.push_back(bouncyBomb);

        Rifle *rifle = new Rifle();
        rifle->itemUserCharacter = this;
        items.push_back(rifle);

        Shotgun *shotgun = new Shotgun();
        shotgun->itemUserCharacter = this;
        items.push_back(shotgun);

        DoomsDay *doomsDay = new DoomsDay();
        doomsDay->itemUserCharacter = this;
        items.push_back(doomsDay);

        Bolter *bolter = new Bolter();
        bolter->itemUserCharacter = this;
        items.push_back(bolter);

        Railgun *railgun = new Railgun();
        railgun->itemUserCharacter = this;
        items.push_back(railgun);

        DirtCannon *dirtCannon = new DirtCannon();
        dirtCannon->itemUserCharacter = this;
        items.push_back(dirtCannon);

        Earthquake *earthquake = new Earthquake();
        //earthquake->itemMana = 100;
        earthquake->itemUserCharacter = this;
        items.push_back(earthquake);

        SpawnVehicle *spawnVehicle = new SpawnVehicle();
        spawnVehicle->itemUserCharacter = this;
        items.push_back(spawnVehicle);

        SpawnRobotGroup *spawnRobotGroup = new SpawnRobotGroup();
        spawnRobotGroup->itemUserCharacter = this;
        items.push_back(spawnRobotGroup);

        FireBall *fireBall = new FireBall();
        fireBall->itemUserCharacter = this;
        items.push_back(fireBall);



        Digger *digger = new Digger();
        digger->itemUserCharacter = this;
        itemsSecondary.push_back(digger);

        JetPack *jetPack = new JetPack();
        jetPack->itemUserCharacter = this;
        itemsSecondary.push_back(jetPack);

        LandGrower *rockGrower = new LandGrower(true, false);
        rockGrower->itemUserCharacter = this;
        itemsSecondary.push_back(rockGrower);

        LandGrower *woodGrower = new LandGrower(false, false);
        woodGrower->itemUserCharacter = this;
        itemsSecondary.push_back(woodGrower);

        LandGrower *rockTileGrower = new LandGrower(true, true);
        rockTileGrower->itemUserCharacter = this;
        itemsSecondary.push_back(rockTileGrower);

        LandGrower *woodTileGrower = new LandGrower(false, true);
        woodTileGrower->itemUserCharacter = this;
        itemsSecondary.push_back(woodTileGrower);


        Repeller *repeller = new Repeller();
        repeller->itemUserCharacter = this;
        itemsSecondary.push_back(repeller);

        LaserSight *laserSight = new LaserSight();
        laserSight->itemUserCharacter = this;
        itemsSecondary.push_back(laserSight);


        collisionHandler.init(*this);
    }

    void addCharacters(vector<Character*> &characters) {
        for(int i=0; i<items.size(); i++) {
            for(int j=0; j<characters.size(); j++) {
                items[i]->addCharacter(characters[j]);
            }
        }
        for(int i=0; i<itemsSecondary.size(); i++) {
            for(int j=0; j<characters.size(); j++) {
                itemsSecondary[i]->addCharacter(characters[j]);
            }
        }
    }


    struct CollisionPoint {
        float angle = 0;
        float dx = 0, dy = 0;

    };

    struct CollisionHandler {
        int numCollisionPoints = 32;
        vector<CollisionPoint> collisionPoints;
        sf::RectangleShape rect;

        CollisionPoint collidingPoint;
        bool isCollision = false;

        void renderCollisionPoints(sf::RenderWindow &window, Character &character) {
            for(int i=0; i<numCollisionPoints; i++) {
                //rect.setPosition(character.x + 0.5 * character.w * character.scaleX + collisionPoints[i].dx, character.y + 0.5* character.h * character.scaleY + collisionPoints[i].dy); TODO remove this

                rect.setPosition(character.x + collisionPoints[i].dx, character.y + collisionPoints[i].dy);

                if(collisionPoints[i].angle >= 0.25 * Pi && collisionPoints[i].angle < 0.75 * Pi) {
                    rect.setFillColor(sf::Color(255, 0, 0, 255));
                }
                if(collisionPoints[i].angle >= 0.75 * Pi && collisionPoints[i].angle < 1.25 * Pi) {
                    rect.setFillColor(sf::Color(255, 255, 0, 255));
                }
                if(collisionPoints[i].angle >= 1.25 * Pi && collisionPoints[i].angle < 1.75 * Pi) {
                    rect.setFillColor(sf::Color(255, 0, 255, 255));
                }
                if((collisionPoints[i].angle >= 1.75 * Pi && collisionPoints[i].angle < 2.0 * Pi) || (collisionPoints[i].angle >= 0.0 * Pi &&
                collisionPoints[i].angle < 0.25 * Pi)) {
                    rect.setFillColor(sf::Color(0, 255, 0, 255));
                }

                window.draw(rect);
            }
        }

        void init(Character &character) {
            rect = sf::RectangleShape(sf::Vector2f(1, 1));
            rect.setFillColor(sf::Color(255, 255, 255, 255));
            rect.setScale(character.scaleX, character.scaleY);
            rect.setOrigin(character.scaleX*0.5, character.scaleY*0.5);


            float initialAngle = 0.5*Pi;
            float deltaAngle = 2.0*Pi / numCollisionPoints;

            float radiusX = character.w*character.scaleX * 0.5;
            float radiusY = character.h*character.scaleY * 0.5;

            for(int i=0; i<numCollisionPoints; i++) {
                CollisionPoint collisionPoint;
                collisionPoint.angle = fmod(initialAngle + i * deltaAngle, 2.0*Pi);
                collisionPoint.dx = radiusX * cos(collisionPoint.angle);
                collisionPoint.dy = radiusY * sin(collisionPoint.angle);
                collisionPoints.push_back(collisionPoint);
            }
        }

        void handleCollisions(Character &character, Map &map) {
            isCollision = false;

            for(int p=0; p<numCollisionPoints; p++) {
                //int i = 0;
                int i = p % numCollisionPoints;
                int j = (i + numCollisionPoints/2) % numCollisionPoints;
                /*float x = character.x + character.w*character.scaleX*0.5;
                float y = character.y + character.h*character.scaleY*0.5;
                //TODO remove this */

                float x = character.x;
                float y = character.y;

                float dx = cos(collisionPoints[i].angle);
                float dy = sin(collisionPoints[i].angle);

                bool iCollides = false, jCollides = false;
                bool buriedInGround = false;
                float tix = 0, tiy = 0;
                float tjx = 0, tjy = 0;


                iCollides = map.checkCollision(x + collisionPoints[i].dx, y + collisionPoints[i].dy);

                //if(iCollides && fabs(collisionPoints[i].dx) >= character.w*character.scaleX*0.1) {
                if(iCollides && collisionPoints[i].dy <= character.h*character.scaleY*0.30) {
                    // TODO check this!
                    if(!isCollision) {
                        collidingPoint = collisionPoints[i];
                    }
                    isCollision = true;
                }

                jCollides = map.checkCollision(x + collisionPoints[j].dx, y + collisionPoints[j].dy);
                int k = 0;
                while(iCollides) {
                    tix = -k * dx;
                    tiy = -k * dy;
                    tjx = -k * dx;
                    tjy = -k * dy;
                    iCollides = map.checkCollision(x + collisionPoints[i].dx + tix, y + collisionPoints[i].dy + tiy);
                    jCollides = map.checkCollision(x + collisionPoints[j].dx + tjx, y + collisionPoints[j].dy + tjy);
                    if(iCollides && jCollides) {
                        buriedInGround = true;
                        break;
                    }
                    if(!iCollides && !jCollides) {
                        break;
                    }
                    k++;
                }
                if(buriedInGround) {
                    //printf("buriedInGround i = %d\n", i);
                    //break;
                }
                if(!iCollides && !jCollides) {
                    //break;
                }
                struct P {
                    int k = 0;
                    int dx = 0;
                    int dy = 0;
                    float getMagnitude() {
                        return sqrt(dx*dx + dy*dy);
                    }
                };
                if(buriedInGround && i == 0) {
                    character.vy = 0;
                    //break;
                    P pUp, pDown, pLeft, pRight;
                    bool upCollides = true;
                    bool downCollides = true;
                    bool leftCollides = true;
                    bool rightCollides = true;
                    while(upCollides) {
                        pUp.dx = 0;
                        pUp.dy = -pUp.k;
                        upCollides = map.checkCollision(x + pUp.dx, y + pUp.dy);
                        pUp.k++;
                        if(pUp.k > 500) break;
                    }
                    //printf("pUp.k %d\n", pUp.k);

                    while(downCollides) {
                        pDown.dx = 0;
                        pDown.dy = pDown.k;
                        downCollides = map.checkCollision(x + pDown.dx, y + pDown.dy);
                        pDown.k++;
                        if(pDown.k > 500) break;
                    }
                    //printf("pDown.k %d\n", pDown.k);

                    while(leftCollides) {
                        pLeft.dx = -pLeft.k;
                        pLeft.dy = 0;
                        leftCollides = map.checkCollision(x + pLeft.dx, y + pLeft.dy);
                        pLeft.k++;
                        if(pLeft.k > 500) break;
                    }
                    //printf("pLeft.k %d\n", pLeft.k);

                    while(rightCollides) {
                        pRight.dx = pRight.k;
                        pRight.dy = 0;
                        rightCollides = map.checkCollision(x + pRight.dx, y + pRight.dy);
                        pRight.k++;
                        if(pRight.k > 500) break;
                    }
                    //printf("pRight.k %d\n", pRight.k);

                    if(pLeft.getMagnitude() <= pUp.getMagnitude() && pLeft.getMagnitude() <= pDown.getMagnitude() && pLeft.getMagnitude() <= pRight.getMagnitude()) {
                        tix = pLeft.dx;
                        tiy = pLeft.dy;
                    }
                    else if(pRight.getMagnitude() <= pUp.getMagnitude() && pRight.getMagnitude() <= pDown.getMagnitude() && pRight.getMagnitude() <= pLeft.getMagnitude()) {
                        tix = pRight.dx;
                        tiy = pRight.dy;
                    }
                    else if(pDown.getMagnitude() <= pUp.getMagnitude()) {
                        tix = pDown.dx;
                        tiy = pDown.dy;
                    }
                    else {
                        tix = pUp.dx;
                        tiy = pUp.dy;
                    }
                    character.x += tix;
                    character.y += tiy;

                    //break;
                }
                else {
                    //character.x += tix;
                    //character.y += tiy;

                    if(fabs(tix) >= 8) {
                        character.x += tix;
                    }
                    else if(fabs(tiy) < 0.001) {
                        character.x += tix;
                    }
                    if(fabs(tiy) >= 8) {
                        character.y += tiy;
                    }
                    else if(fabs(tix) < 0.001) {
                        character.y += tiy;
                    }
                    /*if(fabs(tix) < 3 && fabs(tiy) < 0.001) {
                        character.x += tix;
                    }
                    if(fabs(tiy) <= 3  && fabs(tiy) < 0.001) {
                        character.y += tiy;
                    }*/
                    if(tiy < 0 && fabs(tix) < 0.001) {
                        character.readyToJump = true;
                        character.vy = 0;
                    }
                    if(tiy > 0 && fabs(tix) < 0.001) {
                        character.vy = 0;
                    }
                }
            }

            // TODO fix the hack below:
            if(character.x - character.w * map.scaleX * 0.5 < 0) {
                character.x = character.w * map.scaleX * 0.5;
            }
            if(character.y - character.h * map.scaleY * 0.5 < 0) {
                character.y = character.h * map.scaleY * 0.5;
            }
            if(character.x + character.w * map.scaleX * 0.5 > map.w * map.scaleX) {
                character.x = map.w * map.scaleX - character.w * map.scaleX * 0.5;
            }
            if(character.y + character.h * map.scaleY * 0.5 > map.h * map.scaleY) {
                character.y = map.h * map.scaleY - character.h * map.scaleY * 0.5;
            }
        }
    };

    CollisionHandler collisionHandler;




    inline bool intersects(float px, float py) {
        if(respawning || inThisVehicle) {
            return false;
        }
        return boundingBox.isPointWithin(px, py);
    }






    void update(float dt, int screenW, int screenH, Map &map) {
        if(inThisVehicle) {
            return;
        }

        dt = dt > 1.0/60.0 ? 1.0/60.0 : dt;

        if(respawning) {
            respawnTimer += dt;
            x = lerp(pointOfDeathX, respawnX, respawnTimer / respawnDuration);
            y = lerp(pointOfDeathY, respawnY, respawnTimer / respawnDuration);
            hp = lerp(0, maxHp, respawnTimer / respawnDuration);

            if(respawnTimer >= respawnDuration) {
                respawning = false;
                respawnTimer = 0;

                float tx = x;
                float ty = y;

                float r = 30;
                int px = map.mapX(x, screenW);
                int py = map.mapY(y, screenH);

                for(int i=-r; i<r; i++) {
                    if(px + i < 0 || px + i >= map.w) {
                        continue;
                    }
                    for(int j=-r; j<r; j++) {
                        if(py + j < 0 || py + j >= map.h) {
                            continue;
                        }
                        if(i*i + j*j < r*r) {
                            map.tiles[px+i + (py+j)*map.w] = map.emptyTile;
                        }
                    }
                }
            }
            else {
                return;
            }
        }


        centerX = x;
        centerY = y;    // TODO remove this


        computerControl.update(dt);
        globalAngle = direction == Direction::Right ? aimAngle : Pi - aimAngle;


        itemRepeatTimer += dt;

        if(itemUsing == 1 && itemRepeatTimer >= items[activeItem]->repeatTime() && activeItem >= 0 && activeItem < items.size()) {
            float a = direction == Direction::Right ? aimAngle : Pi - aimAngle;
            if(items[activeItem]->noManaCostPerUse()) {
                items[activeItem]->use(x, y, vx, vy, a);
                itemRepeatTimer = 0;
            }
            else {
                if(items[activeItem]->itemMana >= items[activeItem]->manaCostPerUse()) {
                    items[activeItem]->use(x, y, vx, vy, a);
                    items[activeItem]->itemMana -= items[activeItem]->manaCostPerUse();
                    itemRepeatTimer = 0;
                }
            }
            if(usingItemOnce) {
                usingItemOnce = false;
                itemUsing = 0;
            }
        }

        itemSecondaryRepeatTimer += dt;

        if(itemUsingSecondary == 1 && itemSecondaryRepeatTimer >= itemsSecondary[activeItemSecondary]->repeatTime() && activeItemSecondary >= 0 && activeItemSecondary < itemsSecondary.size()) {
            float a = direction == Direction::Right ? aimAngle : Pi - aimAngle;
            if(itemsSecondary[activeItemSecondary]->noManaCostPerUse()) {
                itemsSecondary[activeItemSecondary]->use(x, y, vx, vy, a);
                itemSecondaryRepeatTimer = 0;
            }
            else {
                if(itemsSecondary[activeItemSecondary]->itemMana >= itemsSecondary[activeItemSecondary]->manaCostPerUse()) {
                    itemsSecondary[activeItemSecondary]->use(x, y, vx, vy, a);
                    itemsSecondary[activeItemSecondary]->itemMana -= itemsSecondary[activeItemSecondary]->manaCostPerUse();
                    itemSecondaryRepeatTimer = 0;
                }
            }
        }

        items[activeItem]->update(map, dt);
        itemsSecondary[activeItemSecondary]->update(map, dt);

        for(int i=0; i<items.size(); i++) {
            if(i == activeItem) continue;
            items[i]->updateNonActive(map, dt);
        }
        for(int i=0; i<itemsSecondary.size(); i++) {
            if(i == activeItemSecondary) continue;
            itemsSecondary[i]->updateNonActive(map, dt);
        }

        for(int i=0; i<items.size(); i++) {
            items[i]->loadMana(map, dt);
        }
        for(int i=0; i<itemsSecondary.size(); i++) {
            itemsSecondary[i]->loadMana(map, dt);
        }

        items[activeItem]->itemMana -= items[activeItem]->manaCostPerSecond() * dt;


        if(aiming == 1) {
            aimAngle -= dt*Pi * 0.5;
            if(aimAngle <= -Pi*0.5) {
                aimAngle = -Pi*0.5;
            }
        }
        if(aiming == 2) {
            aimAngle += dt*Pi * 0.5;
            if(aimAngle >= Pi*0.5) {
                aimAngle = Pi*0.5;
            }
        }
        globalAngle = direction == Direction::Right ? aimAngle : Pi - aimAngle;


        float ax = 0;
        float ay = 0;
        ay += gravity;

        vx += ax*dt;
        vy += ay*dt;

        x += vx * dt * speedFactor;
        y += vy * dt * speedFactor;

        speedFactor = 1.0; // TODO fix this hack



        collisionHandler.handleCollisions(*this, map);

        boundingBox.ax = x - w*scaleX * 0.5;
        boundingBox.ay = y - h*scaleY * 0.5;
        boundingBox.bx = x + w*scaleX * 0.5;
        boundingBox.by = y + h*scaleY * 0.5;


        afterUpdate();

        
        if(doubleClickedItemChangeTimer >= 0) {
            doubleClickedItemChangeTimer += dt;
        }
        if(doubleClickedItemChangeActive > 0) {
            doubleClickedItemChangeActive--;
            if(doubleClickedItemChangeActive == 0) {
                doubleClickedItemChange = false;
            }
        }
        
    }



    void afterUpdate(bool forceInactive = false) {
        if(activeItem >= 0 && activeItem < items.size()) {
            items[activeItem]->afterUpdate(forceInactive);
        }
        if(activeItemSecondary >= 0 && activeItemSecondary < itemsSecondary.size()) {
            itemsSecondary[activeItemSecondary]->afterUpdate(forceInactive);
        }
    }

    float itemRepeatTimer = 0, itemSecondaryRepeatTimer = 0;

    bool usingItemOnce = false;

    void useItem() {
        if(inThisVehicle) {
            inThisVehicle->useItem();
            return;
        }
        itemUsing = 1;
    }
    void stopUseItem() {
        if(inThisVehicle) {
            inThisVehicle->stopUseItem();
            return;
        }
        itemUsing = 0;
    }
    void useItemOnce() {
        if(inThisVehicle) {
            inThisVehicle->useItemOnce();
            return;
        }
        usingItemOnce = true;
        itemUsing = 1;
    }

    void useItemSecondary() {
        if(inThisVehicle) {
            inThisVehicle->useItemSecondary();
            return;
        }
        itemUsingSecondary = 1;
    }

    void stopUseItemSecondary()  {
        if(inThisVehicle) {
            inThisVehicle->stopUseItemSecondary();
            return;
        }
        itemUsingSecondary = 0;
    }

    void jump() {
        if(inThisVehicle) {
            inThisVehicle->jump();
            return;
        }
        if(readyToJump) {
            vy = -0.5*gravity;//*scaleY;
            readyToJump = false;
        }
    }

    int movement = 0;
    int aiming = 0;
    int itemUsing = 0, itemUsingSecondary = 0;

    float speedFactor = 1.0;

    void moveLeft() {
        if(inThisVehicle) {
            inThisVehicle->moveLeft();
            return;
        }
        if(itemChange == ItemChange::ItemChangeInited || itemChange == ItemChange::ItemChangeSecondary) {
            itemChange = ItemChange::ItemChangePrimary;
        }
        else if(itemChange == ItemChange::ItemChangePrimary) {
            activeItem -= 1;
            if(activeItem < 0) {
                activeItem = items.size()-1;
            }
        }
        else {
            vx = -speed*scaleX;
            direction = Direction::Left;
            movement = 1;
        }
    }
    void moveRight() {
        if(inThisVehicle) {
            inThisVehicle->moveRight();
            return;
        }
        if(itemChange == ItemChange::ItemChangeInited || itemChange == ItemChange::ItemChangeSecondary) {
            itemChange = ItemChange::ItemChangePrimary;
        }
        else if(itemChange == ItemChange::ItemChangePrimary) {
            activeItem += 1;
            if(activeItem >= items.size()) {
                activeItem = 0;
            }
        }
        else {
            vx = speed*scaleX;
            direction = Direction::Right;
            movement = 2;
        }
    }
    void stopMoveLeft() {
        if(inThisVehicle) {
            inThisVehicle->stopMoveLeft();
            return;
        }
        if(movement == 1) {
            vx = 0;
        }
    }
    void stopMoveRight() {
        if(inThisVehicle) {
            inThisVehicle->stopMoveRight();
            return;
        }
        if(movement == 2) {
            vx = 0;
        }
    }

    void aimUp() {
        if(inThisVehicle) {
            inThisVehicle->aimUp();
            return;
        }
        if(itemChange == ItemChange::ItemChangeInited || itemChange == ItemChange::ItemChangePrimary) {
            itemChange = ItemChange::ItemChangeSecondary;
        }
        else if(itemChange == ItemChange::ItemChangeSecondary) {
            activeItemSecondary -= 1;
            if(activeItemSecondary < 0) {
                activeItemSecondary = itemsSecondary.size()-1;
            }
        }
        else {
            aiming = 1;
        }
        /*aimAngle -= Pi*0.05;
        if(aimAngle <= -Pi*0.5) {
            aimAngle = -Pi*0.5;
        }*/
    }
    void aimDown() {
        if(inThisVehicle) {
            inThisVehicle->aimDown();
            return;
        }
        if(itemChange == ItemChange::ItemChangeInited || itemChange == ItemChange::ItemChangePrimary) {
            itemChange = ItemChange::ItemChangeSecondary;
        }
        else if(itemChange == ItemChange::ItemChangeSecondary) {
            activeItemSecondary += 1;
            if(activeItemSecondary >= itemsSecondary.size()) {
                activeItemSecondary = 0;
            }
        }
        else {
            aiming = 2;
        }
        /*aimAngle += Pi*0.05;
        if(aimAngle >= Pi*0.5) {
            aimAngle = Pi*0.5;
        }*/
    }
    void stopAimUp() {
        if(inThisVehicle) {
            inThisVehicle->stopAimUp();
            return;
        }
        if(aiming == 1) {
            aiming = 0;
        }
    }
    void stopAimDown() {
        if(inThisVehicle) {
            inThisVehicle->stopAimDown();
            return;
        }
        if(aiming == 2) {
            aiming = 0;
        }
    }

    enum class ItemChange { None, ItemChangeInited, ItemChangePrimary, ItemChangeSecondary };
    ItemChange itemChange = ItemChange::None;

    void changeItem() {
        if(inThisVehicle) {
            inThisVehicle->changeItem();
            return;
        }
        if(itemChange == ItemChange::None) {
            itemChange = ItemChange::ItemChangePrimary;
        }
        else {
            itemChange = ItemChange::None;
        }

        if(doubleClickedItemChangeTimer < 0) {
            doubleClickedItemChangeTimer = 0;
            doubleClickedItemChangeActive = -1;
        }
        else if(doubleClickedItemChangeTimer > 0) {
            if(doubleClickedItemChangeTimer <= doubleClickDurationSeconds) {
                doubleClickedItemChange = true;
                doubleClickedItemChangeActive = 2;
            }
            doubleClickedItemChangeTimer = -1;
        }
    }

    bool respawning = false;
    float respawnTimer = 0;
    float respawnDuration = 5.0;
    float respawnX = 0;
    float respawnY = 0;
    float pointOfDeathX = 0;
    float pointOfDeathY = 0;


    void takeDamage(float amount, float bloodFactor, float velocityFactor, float dirX, float dirY) {
        if(respawning || inThisVehicle) {
            return;
        }
        hp -= amount;
        if(hp <= 0) {
            hp = 0;
            vx = 0;
            vy = 0;
            respawning = true;
            pointOfDeathX = x;
            pointOfDeathY = y;
            respawnX = randf2(50, screenW - 50);
            respawnY = randf2(50, screenH - 50);

            aiming = 0;

            if(items[1]->noManaCostPerUse()) {
                items[1]->use(0, 0, 0, 0, 0);
            }
            if(items[2]->noManaCostPerUse()) {
                items[2]->use(0, 0, 0, 0, 0);
            }
            if(items[6]->noManaCostPerUse()) {
                items[6]->use(0, 0, 0, 0, 0);
            }
            /*if(activeItem == 6 && items[activeItem]->noManaCostPerUse()) {
                items[activeItem]->use(0, 0, 0, 0, 0);
            }*/
            //else {
            //}
            stopUseItem();

            stopUseItemSecondary();
            afterUpdate(true);

            if(computerControl.isActive) {
                computerControl.onDeath();
            }
        }
        if(bloodFactor > 0) {
            float bloodProjectileRadius = max(w, h);
            float numBloodProjectiles = max(1.0, amount*bloodFactor*100.0);
            createBloodBurst(*map, x, y, dirX, dirY, bloodProjectileRadius, numBloodProjectiles, 200*velocityFactor, 800*velocityFactor);
        }
    }


    void createBloodBurst(Map& map, float x, float y, float dirX, float dirY, float bloodProjectileRadius, int numBloodProjectiles, float bloodProjectileVelocityMin, float bloodProjectileVelocityMax);



    void render(sf::RenderWindow &window) {
        if(inThisVehicle) {
            return;
        }
    
        //characterImage.create(w, h, pixels);
        //tileTexture.loadFromImage(tileImage);
        //tileSprite.setTexture(tileTexture, true);
        if(respawning) {
            //characterSprite.setColor(sf::Color(255, 255, 255, 100));
            characterSpriteStanding.setColor(sf::Color(255, 255, 255, 100));
        }
        else {
            characterSpriteStanding.setColor(sf::Color(255, 255, 255, 255));
        }

        sf::Sprite *currentSprite = &characterSpriteStanding;

        if(fabs(vx) > 0 && !respawning) {
            currentSprite = &characterSpriteRunning[runningAnimationCounter];
            runningAnimationCounterX++;
            if(runningAnimationCounterX % 2 == 0) {
                runningAnimationCounter++;
            }
            if(runningAnimationCounter >= numRunningAnimationFrames) {
                runningAnimationCounter = 0;
            }
        }
        else {
            runningAnimationCounter = 0;
        }

        int px = x;
        int py = y;
        px = px / scaleX * scaleX;
        py = py / scaleY * scaleY;

        currentSprite->setPosition(px, py);
        if(direction == Right) {
            currentSprite->setScale(scaleX, scaleY);
        }
        else {
            currentSprite->setScale(-scaleX, scaleY);
        }

        window.draw(*currentSprite);

        /*characterSpriteStanding.setPosition(px, py);
        if(direction == Right) {
            characterSpriteStanding.setScale(scaleX, scaleY);
        }
        else {
            characterSpriteStanding.setScale(-scaleX, scaleY);
        }
        window.draw(characterSpriteStanding);*/

        if(computerControl.isActive) {
            computerControl.render(window);
        }

        items[activeItem]->render(window);
        // TODO render itemsSecondary ??

        /*for(int i=0; i<items.size(); i++) {
            items[i]->render(window);
        }*/
    }

    void renderCrosshair(sf::RenderWindow &window) {
        if(respawning || inThisVehicle) {
            return;
        }
        float dir = direction == Right ? 1 : -1;
        float cx = x;// + w/2;
        float cy = y;// + h/2;
        float cr = 25*scaleX;
        float hx = cx + dir * cos(aimAngle)*cr;
        float hy = cy + sin(aimAngle)*cr; //TODO remove this*/
        /*float cr = 25*scaleX;
        float hx = x + dir * cos(aimAngle)*cr;
        float hy = y + sin(aimAngle)*cr;*/

        crosshairSprite.setPosition(hx, hy);
        crosshairSprite.setScale(scaleX, scaleY);
        window.draw(crosshairSprite);
    }

    bool getCollisionNormal(float px, float py, float &nx, float &ny) {
        float ax = x - w*scaleX * 0.5;
        float ay = y - h*scaleY * 0.5;
        float bx = x + w*scaleX * 0.5;
        float by = y + h*scaleY * 0.5;

        if(!isWithinRect(px, py, ax, ay, bx, by)) {
            return false;
        }

        float dx = px - x;
        float dy = py - y;
        if(dx == 0 && dy == 0) {
            nx = 0;
            ny = 0;
        }
        else {
            float d = sqrt(dx*dx + dy*dy);
            nx = dx / d;
            ny = dy / d;
        }

        return true;
    }

    bool getCollisionReflection(float px, float py, float dx, float dy, float &rx, float & ry) {
        float nx = 0, ny = 0;
        bool collided = getCollisionNormal(px, py, nx, ny);
        if(collided) {
            //float d = sqrt(vx*vx + vy*vy);
            reflectVector(dx, dy, nx, ny, rx, ry);
        }
        return collided;
    }

};








void Walker::render(sf::RenderWindow &window) {

    sf::Sprite *currentSprite = &vehicleSpriteStanding[direction];

    //pixelsVector.clear(); // TODO do these stuff in setup()

    if(fabs(vx) > 0/* && !respawning*/) {
        currentSprite = &vehicleSpriteRunning[direction][runningAnimationCounter];
        //pixelsVector.push_back(vehicleImageRunning[direction][runningAnimationCounter].getPixelsPtr());

        runningAnimationCounterX++;
        if(runningAnimationCounterX % 3 == 0) {
            runningAnimationCounter++;
        }
        if(runningAnimationCounter >= numRunningAnimationFrames) {
            runningAnimationCounter = 0;
        }

        running = true;
    }
    else {
        runningAnimationCounter = 0;
        running = false;
        //pixelsVector.push_back(vehicleImageStanding[direction].getPixelsPtr());
    }

    updatePixelsVectorIndex();

    int px = x;
    int py = y;
    px = px / scaleX * scaleX;
    py = py / scaleY * scaleY;

    currentSprite->setPosition(px, py);
    currentSprite->setScale(scaleX, scaleY);

    window.draw(*currentSprite);

    //if(computerControl.isActive) {
    if(driverCharacter && driverCharacter->computerControl.isActive) {
        computerControl.render(window);
    }

    items[activeItem]->render(window);
    // TODO render itemsSecondary ??
}











void Walker::ComputerControl::init(Map *map, Walker *thisWalker) {
    this->map = map;
    this->thisWalker = thisWalker;
    this->otherCharacters = map->characters;
    for(int i=0; i<otherCharacters.size(); i++) {
        if(otherCharacters[i] == thisWalker->driverCharacter) {
            otherCharacters.erase(otherCharacters.begin()+i);
            break;
        }
    }
}

void Walker::ComputerControl::update(float dt) {
    if(!thisWalker->driverCharacter || !thisWalker->driverCharacter->computerControl.isActive) {
        return;
    }
    /*if(!isActive) {
        return;
    }*/

    if(controlState == ControlState::Free || controlState == ControlState::GoingToCheckPoint) {
        if(thisWalker->items[4]->itemMana >= thisWalker->items[4]->manaCostPerUse()) {
            if(isMelterCannonApplicable() || isMelterCannonApplicableAgainstVehicles()) {
                thisWalker->activeItem = 4;
                //thisWalker->useItem();
                controlStateBeforeMelterCannon = controlState;
                controlState = ControlState::UsingMelterCannon;
                melterCannonTimer = 0;
                melterCannonReactionTime = randf2(melterCannonReactionTimeMin, melterCannonReactionTimeMax);
            }
        }
    }
    if(controlState == ControlState::UsingMelterCannon) {
        melterCannonTimer += dt;
        if(melterCannonTimer >= melterCannonReactionTime) {
            thisWalker->useItem();
        }
        if(melterCannonTimer >= melterCannonDuration) {
            controlState = controlStateBeforeMelterCannon;
            /*if(checkPoints.size() > 0) {
                controlState = ControlState::GoingToCheckPoint;
            }
            else {
                controlState = ControlState::Free;
            }*/
            thisWalker->activeItem = 3;
        }
    }

    opponentVisibilityCheckTimer += dt;
    if(opponentVisibilityCheckTimer >= opponentVisibilityCheckDuration) {
        if(controlState != ControlState::UsingMelterCannon) {
            checkOpponentVisibility();
        }
        opponentVisibilityCheckTimer = 0;
    }


    thisWalker->stopUseItemSecondary();  // TODO check this!


    if(controlState == ControlState::ShootingOpponent) {
        thisWalker->useItem();

        if(thisWalker->items[thisWalker->activeItem]->itemMana < thisWalker->items[thisWalker->activeItem]->manaCostPerUse()) {
            //changeWeapon();
            controlState = ControlState::LoadingWeapon;
            weaponLoadingTimer = 0;
        }
    }
    else if(controlState == ControlState::AttackingOpponentAndShooting) {
        thisWalker->useItem();

        if(thisWalker->items[thisWalker->activeItem]->itemMana < thisWalker->items[thisWalker->activeItem]->manaCostPerUse()) {
            changeWeapon();
            //controlState = ControlState::LoadingWeapon;
            //weaponLoadingTimer = 0;
        }
    }
    else if(controlState != ControlState::UsingMelterCannon) {
        thisWalker->stopUseItem();
    }

    if(controlState == ControlState::LoadingWeapon) {
        weaponLoadingTimer += dt;
        if(weaponLoadingTimer >= weaponLoadingDuration) {
            changeWeapon();
            //controlState = ControlState::ShootingOpponent;
            controlState = ControlState::Free;
            weaponLoadingTimer = 0;
        }
    }

    if(visibleOpponent == nullptr && visibleHostileVehicle == nullptr) {
        opponentLastSeenTimer += dt;
    }

    if(opponentLastSeenTimer >= 10 && (controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting)) {
        controlState = ControlState::Free;
    }

    if(controlState == ControlState::Free) {
        //printf("Free\n");

        checkPointCheckTimer += dt;

        if(checkPointCheckTimer > checkPointCheckDuration) {
            if(checkPoints.size() == 0) {
                CheckPoint checkPoint;
                checkPoint.x = randf2(100, map->w * map->scaleX - 100);
                //checkPoint.y = randf2(50, map->h * map->scaleY - 50);
                checkPoint.y = map->h * map->scaleY - 30;
                checkPoints.push_back(checkPoint);
            }
            checkPointCheckTimer = 0;
            controlState = ControlState::GoingToCheckPoint;
        }

        /*if((thisCharacter->activeItem == 3 || thisCharacter->activeItem == 11) && opponentLastSeenTimer < 2.0 && (opponentLastSeenX != 0 && opponentLastSeenY != 0)) {
            controlState = ControlState::AttackingOpponent;
        }*/ // TODO continue here!
    }


    if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
        //if(checkPoints.size() == 0) {
            checkPoints.clear();

            CheckPoint checkPoint;
            checkPoint.radius = 150;
            checkPoint.x = opponentLastSeenX;
            if(checkPoint.x < 100) {
                checkPoint.x = 100;
            }
            if(checkPoint.x > map->w * map->scaleX - 100) {
                checkPoint.x = map->w * map->scaleX - 100;
            } 
            checkPoint.y = map->h * map->scaleY - 30;
            checkPoints.push_back(checkPoint);
        //}
    }

    thisWalker->stopMoveLeft();
    thisWalker->stopMoveRight();

    if((controlState == ControlState::GoingToCheckPoint || controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) && checkPoints.size() > 0) {

        bool colliding = thisWalker->collisionHandler.isCollision;

        CheckPoint checkPoint = checkPoints[0];

        
        float dx = thisWalker->x - checkPoint.x;
        float dy = thisWalker->y - checkPoint.y;

        if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
            if(visibleOpponent) {
                dx = thisWalker->x - visibleOpponent->x;
                dy = thisWalker->y - visibleOpponent->y;
            }
            else if(visibleHostileVehicle) {
                dx = thisWalker->x - visibleHostileVehicle->x;
                dy = thisWalker->y - visibleHostileVehicle->y;
            }
        }

        float a = atan2(-dy, -dx);

        if((colliding || (a < 0.52*Pi && a > 0.48*Pi)) && controlStateSecondary != ControlStateSecondary::Digging) {
            controlStateSecondary = ControlStateSecondary::Digging;
            diggingTimer = 0;
            for(int k=0; k<thisWalker->itemsSecondary.size(); k++) {
                if(dynamic_cast<Digger*>(thisWalker->itemsSecondary[k])) {
                    thisWalker->activeItemSecondary = k;
                    break;
                }
            }
        }


        if(controlStateSecondary == ControlStateSecondary::Digging) {
            diggingTimer += dt;
            if(diggingTimer >= diggingDuration) {
                controlStateSecondary = ControlStateSecondary::Free;
                thisWalker->stopUseItemSecondary();
            }
            else {
                thisWalker->useItemSecondary();
            }
        }


        if(checkPoint.x + 2 < thisWalker->x) {
            thisWalker->moveLeft();
        }
        else if(checkPoint.x - 2 > thisWalker->x) {
            thisWalker->moveRight();
        }
        if(checkPoint.x < thisWalker->x) {
            thisWalker->aimAngle = atan2(-dy, dx);
        }
        else {
            thisWalker->aimAngle = atan2(-dy, -dx);
        }
        if(controlState == ControlState::AttackingOpponentAndShooting) {
            thisWalker->aimAngle += randf2(-deltaAimAngle, deltaAimAngle);
        }

        if(checkPoint.y < thisWalker->y) {
            thisWalker->jump();
        }
        /*if(checkPoint.y < thisWalker->y && controlStateSecondary == ControlStateSecondary::Free) {
            jetPackTimer = 0;
            controlStateSecondary = ControlStateSecondary::UsingJetPack;
            for(int k=0; k<thisCharacter->itemsSecondary.size(); k++) {
                if(dynamic_cast<JetPack*>(thisCharacter->itemsSecondary[k])) {
                    thisCharacter->activeItemSecondary = k;
                    break;
                }
            }
        }*/



        /*if(controlStateSecondary == ControlStateSecondary::UsingJetPack) {
            jetPackTimer += dt;
            if(jetPackTimer >= jetPackDuration) {
                controlStateSecondary = ControlStateSecondary::Free;
                thisCharacter->stopUseItemSecondary();
            }
            else {
                thisCharacter->useItemSecondary();

                if(controlState != ControlState::GoingToCheckPointAndDroppingNuclearBomb && controlState != ControlState::AttackingOpponent && controlState != ControlState::AttackingOpponentAndShooting) {

                    float d = checkGroundBetweenCharacterAndCheckPoint();

                    jetPackBombingTimer += dt;

                    if(d > 45 * thisCharacter->scaleX && d < 110 * thisCharacter->scaleX && jetPackBombingTimer >= jetPackBombingInterval && fabs(thisCharacter->vy)*thisCharacter->scaleX < Bomb::throwingVelocity) {  // TODO check this!
                        for(int k=0; k<thisCharacter->items.size(); k++) {
                            if(dynamic_cast<Bomb*>(thisCharacter->items[k])) {
                                thisCharacter->activeItem = k;
                                break;
                            }
                        }
                        jetPackBombingTimer = 0;
                        thisCharacter->useItem();
                    }
                }
            }
        }*/


        float tx = thisWalker->x - checkPoint.x;
        float ty = thisWalker->y - checkPoint.y;
        float rr = tx*tx + ty*ty;
        if(rr < checkPoint.radius*checkPoint.radius) {
            checkPoints.clear();
            controlState = ControlState::Free;
        }
    }
}


void Walker::ComputerControl::changeWeapon() {
    if(!visibleOpponent && !visibleHostileVehicle) {
        return;
    }
    float tx = 0;
    float ty = 0;

    if(visibleHostileVehicle) {
        tx = visibleHostileVehicle->x - thisWalker->x;
        ty = visibleHostileVehicle->y - thisWalker->y;
    }
    else {
        tx = visibleOpponent->x - thisWalker->x;
        ty = visibleOpponent->y - thisWalker->y;
    }

    float d = sqrt(tx*tx + ty*ty);
    bool weaponReady = false;
    int i = 0;
    while(!weaponReady) {
        i++;
        if(i > 20) break; // TODO check this!

        float rn = randf2(0, 1);
        if(d < 500) {
            if(rn < 1.0/4.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<HeavyFlamer*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else if(rn < 2.0/4.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<LaserCannon*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else if(rn < 3.0/4.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<ClusterMortar*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<Bolter*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }

        }
        else if(d < 2000) {
            if(rn < 1.0/3.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<LaserCannon*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else if(rn < 2.0/3.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<ClusterMortar*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<Bolter*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
        }
        else {
            if(rn < 1.0/2.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<LaserCannon*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<Bolter*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
        }
        weaponReady = thisWalker->items[thisWalker->activeItem]->itemMana >= thisWalker->items[thisWalker->activeItem]->manaCostPerUse();
    }
}


float Walker::ComputerControl::checkGroundBetweenCharacterAndCheckPoint() {
    if(checkPoints.size() == 0) return -1;

    float tx = checkPoints[0].x - thisWalker->x;
    float ty = checkPoints[0].y - thisWalker->y;
    float distanceToCheckPoint = sqrt(tx*tx + ty*ty);

    float dx = tx / distanceToCheckPoint;
    float dy = ty / distanceToCheckPoint;
    for(int k=0; k<5000; k++) {

        float x = thisWalker->x + dx*k;
        float y = thisWalker->y + dy*k;

        if(map->checkCollision(x, y)) {
            float distanceToGround = sqrt(dx*k*dx*k + dy*k*dy*k);
            if(distanceToCheckPoint < distanceToGround) {
                return -1;
            }
            else {
                return distanceToGround;
            }
        }
    }
    return -1;
}


float Walker::ComputerControl::distanceToGroundBelowCharacter() {
    float dy = 1;

    float x = thisWalker->x;
    float characterY = thisWalker->y;

    for(int k=0; k<5000; k++) {
        float y = characterY + dy*k;

        if(map->checkCollision(x, y)) {
            float distanceToGround = sqrt(dy*k*dy*k);
            return distanceToGround;
        }
    }
    return -1;
}

bool Walker::ComputerControl::isMelterCannonApplicable() {
    for(int i=0; i<otherCharacters.size(); i++) {
        if(otherCharacters[i]->respawning || otherCharacters[i] == thisWalker->driverCharacter || otherCharacters[i]->inThisVehicle) {
            continue;
        }
        float tx = otherCharacters[i]->x - thisWalker->x;
        float ty = otherCharacters[i]->y - thisWalker->y;
        float d = sqrt(tx*tx + ty*ty);
        if(d == 0) continue;
        float dx = tx / d;
        float dy = ty / d;

        int hitsGround = 0;

        int n = min(round(d), 490);

        for(int i=0; i<n; i++) {
            if(map->checkCollision(thisWalker->x + dx * i, thisWalker->y + dy * i)) {
                hitsGround++;
            }
        }
        bool freeTile = false;
        if(d < 500) {
            freeTile = !map->checkCollision(otherCharacters[i]->x, otherCharacters[i]->y);
        }
        else {
            freeTile = !map->checkCollision(thisWalker->x + dx * 500, thisWalker->y + dy * 500);
        }

        if(hitsGround >= 100 && freeTile) {
            if(otherCharacters[i]->x < thisWalker->x) {
                thisWalker->direction = Vehicle::Direction::Left;
                thisWalker->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
            }
            else {
                thisWalker->direction = Vehicle::Direction::Right;
                thisWalker->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
            }

            return true;
        }

    }
    return false;
}


bool Walker::ComputerControl::isMelterCannonApplicableAgainstVehicles() {
    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->driverCharacter == nullptr || vehicles[i] == thisWalker) {
            continue;
        }
        float tx = vehicles[i]->x - thisWalker->x;
        float ty = vehicles[i]->y - thisWalker->y;
        float d = sqrt(tx*tx + ty*ty);
        if(d == 0) continue;
        float dx = tx / d;
        float dy = ty / d;

        int hitsGround = 0;

        int n = min(round(d), 490);

        for(int i=0; i<n; i++) {
            if(map->checkCollision(thisWalker->x + dx * i, thisWalker->y + dy * i)) {
                hitsGround++;
            }
        }
        bool freeTile = false;
        if(d < 500) {
            freeTile = !map->checkCollision(vehicles[i]->x, vehicles[i]->y);
        }
        else {
            freeTile = !map->checkCollision(thisWalker->x + dx * 500, thisWalker->y + dy * 500);
        }

        if(hitsGround >= 100 && freeTile) {
            if(vehicles[i]->x < thisWalker->x) {
                thisWalker->direction = Vehicle::Direction::Left;
                thisWalker->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
            }
            else {
                thisWalker->direction = Vehicle::Direction::Right;
                thisWalker->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
            }

            return true;
        }

    }
    return false;
}





void Walker::ComputerControl::checkOpponentVisibility() {
    if(controlState == ControlState::LoadingWeapon) {
        return;
    }

    visibleOpponent = nullptr;
    bool opponentVisible = false;

    for(int i=0; i<otherCharacters.size(); i++) {
        if(otherCharacters[i]->respawning || otherCharacters[i] == thisWalker->driverCharacter) {
            continue;
        }
        float tx = otherCharacters[i]->x - thisWalker->x;
        float ty = otherCharacters[i]->y - thisWalker->y;
        float d = sqrt(tx*tx + ty*ty);
        float dx = tx / d;
        float dy = ty / d;
        float rr = 500;
        for(int k=0; k<5000; k++) {
            float x = thisWalker->x + dx*k;
            float y = thisWalker->y + dy*k;

            if(map->checkCollision(x, y)) {
                break;
            }

            float vx = otherCharacters[i]->x - x;
            float vy = otherCharacters[i]->y - y;
            

            if(vx*vx + vy*vy < rr) {
                visibleOpponent = otherCharacters[i];
                opponentVisible = true;
                opponentLastSeenX = otherCharacters[i]->x;
                opponentLastSeenY = otherCharacters[i]->y;
                opponentLastSeenTimer = 0;

                if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
                    controlState = ControlState::AttackingOpponentAndShooting;
                }
                else {
                    controlState = ControlState::ShootingOpponent;
                }

                if(visibleOpponent->x < thisWalker->x) {
                    thisWalker->direction = Vehicle::Direction::Left;
                    if(thisWalker->activeItem == 2) { //TODO fix this hack!
                        if(d < 500) {
                            thisWalker->aimAngle = atan2(dy, -dx) - 0.05 + randf2(-deltaAimAngle, deltaAimAngle);
                        }
                        else if(d < 1000) {
                            thisWalker->aimAngle = -0.1 + randf2(-deltaAimAngle, deltaAimAngle);
                        }
                        else if(d < 1500) {
                            thisWalker->aimAngle = -0.3 + randf2(-deltaAimAngle, deltaAimAngle);
                        }
                        else {
                            thisWalker->aimAngle = -0.6 + randf2(-deltaAimAngle, deltaAimAngle);
                        }
                    }
                    else {
                        thisWalker->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }
                }
                else {
                    thisWalker->direction = Vehicle::Direction::Right;
                    if(thisWalker->activeItem == 2) { //TODO fix this hack!
                        if(d < 500) {
                            thisWalker->aimAngle = atan2(dy, dx) - 0.05 + randf2(-deltaAimAngle, deltaAimAngle);
                        }
                        else if(d < 1000) {
                            thisWalker->aimAngle = -0.1 + randf2(-deltaAimAngle, deltaAimAngle);
                        }
                        else if(d < 1500) {
                            thisWalker->aimAngle = -0.3 + randf2(-deltaAimAngle, deltaAimAngle);
                        }
                        else {
                            thisWalker->aimAngle = -0.6 + randf2(-deltaAimAngle, deltaAimAngle);
                        }
                    }
                    else {
                        thisWalker->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }
                }

                break;
            }
        }
        if(opponentVisible) {
            break;
        }
    }




    bool hostileVehicleVisible = false;
    visibleHostileVehicle = nullptr;

    if(!opponentVisible) {

        for(int i=0; i<vehicles.size(); i++) {
            if((vehicles[i]->driverCharacter == nullptr && dynamic_cast<Walker*>(vehicles[i])) || vehicles[i] == thisWalker || vehicles[i]->driverCharacter == thisWalker->driverCharacter) {
                continue;
            }
            float tx = vehicles[i]->x - thisWalker->x;
            float ty = vehicles[i]->y - thisWalker->y;
            float d = sqrt(tx*tx + ty*ty);
            float dx = tx / d;
            float dy = ty / d;
            float rr = 500;
            for(int k=0; k<5000; k++) {
                float x = thisWalker->x + dx*k;
                float y = thisWalker->y + dy*k;

                if(map->checkCollision(x, y)) {
                    break;
                }

                float vx = vehicles[i]->x - x;
                float vy = vehicles[i]->y - y;

                if(vx*vx + vy*vy < rr) {
                    visibleHostileVehicle = vehicles[i];
                    hostileVehicleVisible = true;
                    opponentLastSeenX = vehicles[i]->x;
                    opponentLastSeenY = vehicles[i]->y;
                    opponentLastSeenTimer = 0;

                    if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
                        controlState = ControlState::AttackingOpponentAndShooting;
                    }
                    else {
                        controlState = ControlState::ShootingOpponent;
                    }


                    if(visibleHostileVehicle->x < thisWalker->x) {
                        thisWalker->direction = Vehicle::Direction::Left;
                        if(thisWalker->activeItem == 2) { //TODO fix this hack!
                            if(d < 500) {
                                thisWalker->aimAngle = atan2(dy, -dx) - 0.05 + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                            else if(d < 1000) {
                                thisWalker->aimAngle = -0.1 + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                            else if(d < 1500) {
                                thisWalker->aimAngle = -0.3 + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                            else {
                                thisWalker->aimAngle = -0.6 + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                        }
                        else {
                            thisWalker->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
                        }
                    }
                    else {
                        thisWalker->direction = Vehicle::Direction::Right;
                        if(thisWalker->activeItem == 2) { //TODO fix this hack!
                            if(d < 500) {
                                thisWalker->aimAngle = atan2(dy, dx) - 0.05 + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                            else if(d < 1000) {
                                thisWalker->aimAngle = -0.1 + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                            else if(d < 1500) {
                                thisWalker->aimAngle = -0.3 + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                            else {
                                thisWalker->aimAngle = -0.6 + randf2(-deltaAimAngle, deltaAimAngle);
                            }
                        }
                        else {
                            thisWalker->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
                        }
                    }

                    /*if(visibleHostileVehicle->x < thisWalker->x) {
                        thisWalker->direction = Vehicle::Direction::Left;
                        thisWalker->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }
                    else {
                        thisWalker->direction = Vehicle::Direction::Right;
                        thisWalker->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }*/

                    break;
                }
            }
            if(hostileVehicleVisible) {
                break;
            }
        }

    }





    if(!opponentVisible && !hostileVehicleVisible && controlState == ControlState::ShootingOpponent) {
        if(randf2(0, 1) < probabilityOfStartingAnAttack) {
            checkPoints.clear();

            CheckPoint checkPoint;
            checkPoint.radius = 150;
            checkPoint.x = opponentLastSeenX + randf2(-1, 1);
            if(checkPoint.x < 100) {
                checkPoint.x = 100;
            }
            if(checkPoint.x > map->w * map->scaleX - 100) {
                checkPoint.x = map->w * map->scaleX - 100;
            } 
            //checkPoint.y = opponentLastSeenY + randf2(-1, 1);
            checkPoint.y = map->h * map->scaleY - 30;
            checkPoints.push_back(checkPoint);

            controlState = ControlState::AttackingOpponent;
        }
        else {
            checkPoints.clear();
            CheckPoint checkPoint;
            checkPoint.x = randf2(100, map->w * map->scaleX - 100);
            //checkPoint.y = randf2(50, map->h * map->scaleY - 50);
            checkPoint.y = map->h * map->scaleY - 30;
            checkPoints.push_back(checkPoint);

            controlState = ControlState::Free;
        }
    }

    if(!opponentVisible && !hostileVehicleVisible && controlState == ControlState::AttackingOpponentAndShooting) {
        controlState = ControlState::AttackingOpponent;
    }

}










































void Robot::render(sf::RenderWindow &window) {

    sf::Sprite *currentSprite = &vehicleSpriteStanding[direction];

    //pixelsVector.clear(); // TODO do these stuff in setup()

    if(fabs(vx) > 0/* && !respawning*/) {
        currentSprite = &vehicleSpriteRunning[direction][runningAnimationCounter];
        //pixelsVector.push_back(vehicleImageRunning[direction][runningAnimationCounter].getPixelsPtr());

        runningAnimationCounterX++;
        if(runningAnimationCounterX % 3 == 0) {
            runningAnimationCounter++;
        }
        if(runningAnimationCounter >= numRunningAnimationFrames) {
            runningAnimationCounter = 0;
        }

        running = true;
    }
    else {
        runningAnimationCounter = 0;
        running = false;
        //pixelsVector.push_back(vehicleImageStanding[direction].getPixelsPtr());
    }

    updatePixelsVectorIndex();

    int px = x;
    int py = y;
    px = px / scaleX * scaleX;
    py = py / scaleY * scaleY;

    currentSprite->setPosition(px, py);
    currentSprite->setScale(scaleX, scaleY);

    window.draw(*currentSprite);

    if(computerControl.isActive) {
    //if(driverCharacter) {
        computerControl.render(window);
    }

    items[activeItem]->render(window);
    // TODO render itemsSecondary ??
}











void Robot::ComputerControl::init(Map *map, Robot *thisRobot) {
    this->map = map;
    this->thisRobot = thisRobot;
    this->otherCharacters = map->characters;
    for(int i=0; i<otherCharacters.size(); i++) {
        if(otherCharacters[i] == thisRobot->driverCharacter) {
            otherCharacters.erase(otherCharacters.begin()+i);
            break;
        }
    }
}

void Robot::ComputerControl::update(float dt) {
    /*if(!thisRobot->driverCharacter || !thisRobot->driverCharacter->computerControl.isActive) {
        return;
    }*/
    /*if(!isActive) {
        return;
    }*/

    /*if(!thisRobot->remoteControlActive) {
        return;
    }*/

    /*if(controlState == ControlState::Free || controlState == ControlState::GoingToCheckPoint) {
        if(thisWalker->items[4]->itemMana >= thisWalker->items[4]->manaCostPerUse()) {
            if(isMelterCannonApplicable() || isMelterCannonApplicableAgainstVehicles()) {
                thisWalker->activeItem = 4;
                //thisWalker->useItem();
                controlStateBeforeMelterCannon = controlState;
                controlState = ControlState::UsingMelterCannon;
                melterCannonTimer = 0;
                melterCannonReactionTime = randf2(melterCannonReactionTimeMin, melterCannonReactionTimeMax);
            }
        }
    }
    if(controlState == ControlState::UsingMelterCannon) {
        melterCannonTimer += dt;
        if(melterCannonTimer >= melterCannonReactionTime) {
            thisWalker->useItem();
        }
        if(melterCannonTimer >= melterCannonDuration) {
            controlState = controlStateBeforeMelterCannon;

            thisWalker->activeItem = 3;
        }
    }*/

    opponentVisibilityCheckTimer += dt;
    if(opponentVisibilityCheckTimer >= opponentVisibilityCheckDuration) {
        if(controlState != ControlState::UsingMelterCannon) {
            checkOpponentVisibility();
        }
        opponentVisibilityCheckTimer = 0;
    }


    thisRobot->stopUseItemSecondary();  // TODO check this!


    if(controlState == ControlState::ShootingOpponent) {
        thisRobot->useItem();

        if(thisRobot->items[thisRobot->activeItem]->itemMana < thisRobot->items[thisRobot->activeItem]->manaCostPerUse()) {
            //changeWeapon();
            controlState = ControlState::LoadingWeapon;
            weaponLoadingTimer = 0;
        }
    }
    else if(controlState == ControlState::AttackingOpponentAndShooting) {
        thisRobot->useItem();

        if(thisRobot->items[thisRobot->activeItem]->itemMana < thisRobot->items[thisRobot->activeItem]->manaCostPerUse()) {
            changeWeapon();
            //controlState = ControlState::LoadingWeapon;
            //weaponLoadingTimer = 0;
        }
    }
    else { //if(controlState != ControlState::UsingMelterCannon) {
        thisRobot->stopUseItem();
    }

    if(controlState == ControlState::LoadingWeapon) {
        weaponLoadingTimer += dt;
        if(weaponLoadingTimer >= weaponLoadingDuration) {
            changeWeapon();
            //controlState = ControlState::ShootingOpponent;
            controlState = ControlState::Free;
            weaponLoadingTimer = 0;
        }
    }

    if(visibleOpponent == nullptr && visibleHostileVehicle == nullptr) {
        opponentLastSeenTimer += dt;
    }

    if(opponentLastSeenTimer >= 10 && (controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting)) {
        controlState = ControlState::Free;
    }

    if(controlState == ControlState::Free) {
        //printf("Free\n");

        checkPointCheckTimer += dt;

        if(checkPointCheckTimer > checkPointCheckDuration) {
            if(checkPoints.size() == 0) {
                CheckPoint checkPoint;
                checkPoint.x = randf2(50, map->w * map->scaleX - 50);
                checkPoint.y = randf2(50, map->h * map->scaleY - 50);
                checkPoints.push_back(checkPoint);
            }
            checkPointCheckTimer = 0;
            controlState = ControlState::GoingToCheckPoint;
        }

        /*if((thisCharacter->activeItem == 3 || thisCharacter->activeItem == 11) && opponentLastSeenTimer < 2.0 && (opponentLastSeenX != 0 && opponentLastSeenY != 0)) {
            controlState = ControlState::AttackingOpponent;
        }*/ // TODO continue here!
    }


    if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
        //if(checkPoints.size() == 0) {
            checkPoints.clear();

            CheckPoint checkPoint;
            checkPoint.radius = 150;
            checkPoint.x = opponentLastSeenX;
            checkPoint.y = opponentLastSeenY;
            checkPoints.push_back(checkPoint);
        //}
    }

    thisRobot->stopMoveLeft();
    thisRobot->stopMoveRight();

    if((controlState == ControlState::GoingToCheckPoint || controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) && checkPoints.size() > 0) {

        bool colliding = thisRobot->collisionHandler.isCollision;

        CheckPoint checkPoint = checkPoints[0];

        
        float dx = thisRobot->x - checkPoint.x;
        float dy = thisRobot->y - checkPoint.y;

        if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
            if(visibleOpponent) {
                dx = thisRobot->x - visibleOpponent->x;
                dy = thisRobot->y - visibleOpponent->y;
            }
            else if(visibleHostileVehicle) {
                dx = thisRobot->x - visibleHostileVehicle->x;
                dy = thisRobot->y - visibleHostileVehicle->y;
            }
        }

        float a = atan2(-dy, -dx);

        if((colliding || (a < 0.52*Pi && a > 0.48*Pi)) && controlStateSecondary != ControlStateSecondary::Digging) {
            controlStateSecondary = ControlStateSecondary::Digging;
            diggingTimer = 0;
            for(int k=0; k<thisRobot->itemsSecondary.size(); k++) {
                if(dynamic_cast<Digger*>(thisRobot->itemsSecondary[k])) {
                    thisRobot->activeItemSecondary = k;
                    break;
                }
            }
        }


        if(controlStateSecondary == ControlStateSecondary::Digging) {
            diggingTimer += dt;
            if(diggingTimer >= diggingDuration) {
                controlStateSecondary = ControlStateSecondary::Free;
                thisRobot->stopUseItemSecondary();
            }
            else {
                thisRobot->useItemSecondary();
            }
        }


        if(checkPoint.x + 2 < thisRobot->x) {
            thisRobot->moveLeft();
        }
        else if(checkPoint.x - 2 > thisRobot->x) {
            thisRobot->moveRight();
        }
        if(checkPoint.x < thisRobot->x) {
            thisRobot->aimAngle = atan2(-dy, dx);
        }
        else {
            thisRobot->aimAngle = atan2(-dy, -dx);
        }
        if(controlState == ControlState::AttackingOpponentAndShooting) {
            thisRobot->aimAngle += randf2(-deltaAimAngle, deltaAimAngle);
        }

        if(checkPoint.y < thisRobot->y) {
            thisRobot->jump();
        }
        if(checkPoint.y < thisRobot->y && controlStateSecondary == ControlStateSecondary::Free) {
            jetPackTimer = 0;
            controlStateSecondary = ControlStateSecondary::UsingJetPack;
            for(int k=0; k<thisRobot->itemsSecondary.size(); k++) {
                if(dynamic_cast<JetPack*>(thisRobot->itemsSecondary[k])) {
                    thisRobot->activeItemSecondary = k;
                    break;
                }
            }
        }



        if(controlStateSecondary == ControlStateSecondary::UsingJetPack) {
            jetPackTimer += dt;
            if(jetPackTimer >= jetPackDuration) {
                controlStateSecondary = ControlStateSecondary::Free;
                thisRobot->stopUseItemSecondary();
            }
            else {
                thisRobot->useItemSecondary();

                /*if(controlState != ControlState::GoingToCheckPointAndDroppingNuclearBomb && controlState != ControlState::AttackingOpponent && controlState != ControlState::AttackingOpponentAndShooting) {

                    float d = checkGroundBetweenCharacterAndCheckPoint();

                    jetPackBombingTimer += dt;

                    if(d > 45 * thisCharacter->scaleX && d < 110 * thisCharacter->scaleX && jetPackBombingTimer >= jetPackBombingInterval && fabs(thisCharacter->vy)*thisCharacter->scaleX < Bomb::throwingVelocity) {  // TODO check this!
                        for(int k=0; k<thisCharacter->items.size(); k++) {
                            if(dynamic_cast<Bomb*>(thisCharacter->items[k])) {
                                thisCharacter->activeItem = k;
                                break;
                            }
                        }
                        jetPackBombingTimer = 0;
                        thisCharacter->useItem();
                    }
                }*/
            }
        }


        float tx = thisRobot->x - checkPoint.x;
        float ty = thisRobot->y - checkPoint.y;
        float rr = tx*tx + ty*ty;
        if(rr < checkPoint.radius*checkPoint.radius) {
            checkPoints.clear();
            controlState = ControlState::Free;
        }
    }
}


void Robot::ComputerControl::changeWeapon() {
    if(!visibleOpponent && !visibleHostileVehicle) {
        return;
    }
    float tx = 0;
    float ty = 0;

    if(visibleHostileVehicle) {
        tx = visibleHostileVehicle->x - thisRobot->x;
        ty = visibleHostileVehicle->y - thisRobot->y;
    }
    else {
        tx = visibleOpponent->x - thisRobot->x;
        ty = visibleOpponent->y - thisRobot->y;
    }

    float d = sqrt(tx*tx + ty*ty);
    //bool weaponReady = false;
    bool weaponReady = true;
    int i = 0;
    while(!weaponReady) {
        i++;
        if(i > 20) break; // TODO check this!

        float rn = randf2(0, 1);
        /*if(d < 500) {
            if(rn < 1.0/4.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<HeavyFlamer*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else if(rn < 2.0/4.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<LaserCannon*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else if(rn < 3.0/4.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<ClusterMortar*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<Bolter*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }

        }
        else if(d < 2000) {
            if(rn < 1.0/3.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<LaserCannon*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else if(rn < 2.0/3.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<ClusterMortar*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<Bolter*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
        }
        else {
            if(rn < 1.0/2.0) {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<LaserCannon*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
            else {
                for(int k=0; k<thisWalker->items.size(); k++) {
                    if(dynamic_cast<Bolter*>(thisWalker->items[k])) {
                        thisWalker->activeItem = k;
                        break;
                    }
                }
            }
        }*/
        //weaponReady = thisRobot->items[thisRobot->activeItem]->itemMana >= thisRobot->items[thisRobot->activeItem]->manaCostPerUse();
    }
}


float Robot::ComputerControl::checkGroundBetweenCharacterAndCheckPoint() {
    if(checkPoints.size() == 0) return -1;

    float tx = checkPoints[0].x - thisRobot->x;
    float ty = checkPoints[0].y - thisRobot->y;
    float distanceToCheckPoint = sqrt(tx*tx + ty*ty);

    float dx = tx / distanceToCheckPoint;
    float dy = ty / distanceToCheckPoint;
    for(int k=0; k<5000; k++) {

        float x = thisRobot->x + dx*k;
        float y = thisRobot->y + dy*k;

        if(map->checkCollision(x, y)) {
            float distanceToGround = sqrt(dx*k*dx*k + dy*k*dy*k);
            if(distanceToCheckPoint < distanceToGround) {
                return -1;
            }
            else {
                return distanceToGround;
            }
        }
    }
    return -1;
}


float Robot::ComputerControl::distanceToGroundBelowCharacter() {
    float dy = 1;

    float x = thisRobot->x;
    float characterY = thisRobot->y;

    for(int k=0; k<5000; k++) {
        float y = characterY + dy*k;

        if(map->checkCollision(x, y)) {
            float distanceToGround = sqrt(dy*k*dy*k);
            return distanceToGround;
        }
    }
    return -1;
}
/*
bool Robot::ComputerControl::isMelterCannonApplicable() {
    for(int i=0; i<otherCharacters.size(); i++) {
        if(otherCharacters[i]->respawning || otherCharacters[i] == thisRobot->driverCharacter || otherCharacters[i]->inThisVehicle) {
            continue;
        }
        float tx = otherCharacters[i]->x - thisRobot->x;
        float ty = otherCharacters[i]->y - thisRobot->y;
        float d = sqrt(tx*tx + ty*ty);
        if(d == 0) continue;
        float dx = tx / d;
        float dy = ty / d;

        int hitsGround = 0;

        int n = min(round(d), 490);

        for(int i=0; i<n; i++) {
            if(map->checkCollision(thisRobot->x + dx * i, thisRobot->y + dy * i)) {
                hitsGround++;
            }
        }
        bool freeTile = false;
        if(d < 500) {
            freeTile = !map->checkCollision(otherCharacters[i]->x, otherCharacters[i]->y);
        }
        else {
            freeTile = !map->checkCollision(thisRobot->x + dx * 500, thisRobot->y + dy * 500);
        }

        if(hitsGround >= 100 && freeTile) {
            if(otherCharacters[i]->x < thisRobot->x) {
                thisRobot->direction = Vehicle::Direction::Left;
                thisRobot->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
            }
            else {
                thisRobot->direction = Vehicle::Direction::Right;
                thisRobot->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
            }

            return true;
        }

    }
    return false;
}


bool Robot::ComputerControl::isMelterCannonApplicableAgainstVehicles() {
    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->driverCharacter == nullptr || vehicles[i] == thisRobot) {
            continue;
        }
        float tx = vehicles[i]->x - thisRobot->x;
        float ty = vehicles[i]->y - thisRobot->y;
        float d = sqrt(tx*tx + ty*ty);
        if(d == 0) continue;
        float dx = tx / d;
        float dy = ty / d;

        int hitsGround = 0;

        int n = min(round(d), 490);

        for(int i=0; i<n; i++) {
            if(map->checkCollision(thisRobot->x + dx * i, thisRobot->y + dy * i)) {
                hitsGround++;
            }
        }
        bool freeTile = false;
        if(d < 500) {
            freeTile = !map->checkCollision(vehicles[i]->x, vehicles[i]->y);
        }
        else {
            freeTile = !map->checkCollision(thisRobot->x + dx * 500, thisRobot->y + dy * 500);
        }

        if(hitsGround >= 100 && freeTile) {
            if(vehicles[i]->x < thisRobot->x) {
                thisRobot->direction = Vehicle::Direction::Left;
                thisRobot->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
            }
            else {
                thisRobot->direction = Vehicle::Direction::Right;
                thisRobot->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
            }

            return true;
        }

    }
    return false;
}*/





void Robot::ComputerControl::checkOpponentVisibility() {
    if(controlState == ControlState::LoadingWeapon) {
        return;
    }

    visibleOpponent = nullptr;
    bool opponentVisible = false;

    for(int i=0; i<otherCharacters.size(); i++) {
        if(otherCharacters[i]->respawning || otherCharacters[i] == thisRobot->driverCharacter) {
            continue;
        }
        float tx = otherCharacters[i]->x - thisRobot->x;
        float ty = otherCharacters[i]->y - thisRobot->y;
        float d = sqrt(tx*tx + ty*ty);
        float dx = tx / d;
        float dy = ty / d;
        float rr = 500;
        for(int k=0; k<5000; k++) {
            float x = thisRobot->x + dx*k;
            float y = thisRobot->y + dy*k;

            if(map->checkCollision(x, y)) {
                break;
            }

            float vx = otherCharacters[i]->x - x;
            float vy = otherCharacters[i]->y - y;
            

            if(vx*vx + vy*vy < rr) {
                visibleOpponent = otherCharacters[i];
                opponentVisible = true;
                opponentLastSeenX = otherCharacters[i]->x;
                opponentLastSeenY = otherCharacters[i]->y;
                opponentLastSeenTimer = 0;

                if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
                    controlState = ControlState::AttackingOpponentAndShooting;
                }
                else {
                    controlState = ControlState::ShootingOpponent;
                }

                if(visibleOpponent->x < thisRobot->x) {
                    thisRobot->direction = Vehicle::Direction::Left;

                    thisRobot->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
                }
                else {
                    thisRobot->direction = Vehicle::Direction::Right;

                    thisRobot->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
                }

                break;
            }
        }
        if(opponentVisible) {
            break;
        }
    }




    bool hostileVehicleVisible = false;
    visibleHostileVehicle = nullptr;

    if(!opponentVisible) {

        for(int i=0; i<vehicles.size(); i++) {
            if(vehicles[i]->driverCharacter == nullptr || vehicles[i] == thisRobot || vehicles[i]->driverCharacter == thisRobot->driverCharacter) {
                continue;
            }
            float tx = vehicles[i]->x - thisRobot->x;
            float ty = vehicles[i]->y - thisRobot->y;
            float d = sqrt(tx*tx + ty*ty);
            float dx = tx / d;
            float dy = ty / d;
            float rr = 500;
            for(int k=0; k<5000; k++) {
                float x = thisRobot->x + dx*k;
                float y = thisRobot->y + dy*k;

                if(map->checkCollision(x, y)) {
                    break;
                }

                float vx = vehicles[i]->x - x;
                float vy = vehicles[i]->y - y;

                if(vx*vx + vy*vy < rr) {
                    visibleHostileVehicle = vehicles[i];
                    hostileVehicleVisible = true;
                    opponentLastSeenX = vehicles[i]->x;
                    opponentLastSeenY = vehicles[i]->y;
                    opponentLastSeenTimer = 0;

                    if(controlState == ControlState::AttackingOpponent || controlState == ControlState::AttackingOpponentAndShooting) {
                        controlState = ControlState::AttackingOpponentAndShooting;
                    }
                    else {
                        controlState = ControlState::ShootingOpponent;
                    }


                    if(visibleHostileVehicle->x < thisRobot->x) {
                        thisRobot->direction = Vehicle::Direction::Left;

                        thisRobot->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }
                    else {
                        thisRobot->direction = Vehicle::Direction::Right;

                        thisRobot->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }

                    /*if(visibleHostileVehicle->x < thisWalker->x) {
                        thisWalker->direction = Vehicle::Direction::Left;
                        thisWalker->aimAngle = atan2(dy, -dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }
                    else {
                        thisWalker->direction = Vehicle::Direction::Right;
                        thisWalker->aimAngle = atan2(dy, dx) + randf2(-deltaAimAngle, deltaAimAngle);
                    }*/

                    break;
                }
            }
            if(hostileVehicleVisible) {
                break;
            }
        }

    }





    if(!opponentVisible && !hostileVehicleVisible && controlState == ControlState::ShootingOpponent) {
        if(randf2(0, 1) < probabilityOfStartingAnAttack) {
            checkPoints.clear();

            CheckPoint checkPoint;
            checkPoint.radius = 150;
            checkPoint.x = opponentLastSeenX + randf2(-1, 1);
            if(checkPoint.x < 100) {
                checkPoint.x = 100;
            }
            if(checkPoint.x > map->w * map->scaleX - 100) {
                checkPoint.x = map->w * map->scaleX - 100;
            } 
            //checkPoint.y = opponentLastSeenY + randf2(-1, 1);
            checkPoint.y = map->h * map->scaleY - 30;
            checkPoints.push_back(checkPoint);

            controlState = ControlState::AttackingOpponent;
        }
        else {
            checkPoints.clear();
            CheckPoint checkPoint;
            checkPoint.x = randf2(100, map->w * map->scaleX - 100);
            //checkPoint.y = randf2(50, map->h * map->scaleY - 50);
            checkPoint.y = map->h * map->scaleY - 30;
            checkPoints.push_back(checkPoint);

            controlState = ControlState::Free;
        }
    }

    if(!opponentVisible && !hostileVehicleVisible && controlState == ControlState::AttackingOpponentAndShooting) {
        controlState = ControlState::AttackingOpponent;
    }

}









































void Map::render(float screenW, float screenH, sf::RenderWindow &window) {
    float mw = w * scaleX;
    float mh = h * scaleY;
    float mx = screenW/2 - mw/2;
    float my = screenH/2 - mh/2;

    /*for(int i=0; i<numCores; i++) {
        if(mapRenderThreads.size() > i && mapRenderThreads[i].joinable()) {
            mapRenderThreads[i].join();
        }
    }
    mapRenderThreads.clear();*/

    float radius = 180;
    float ax[] = { max(characters[0]->centerX - radius, 0.0f), max(characters[1]->centerX - radius, 0.0f)};
    float ay[] = { max(characters[0]->centerY - radius, 0.0f), max(characters[1]->centerY - radius, 0.0f) };
    float bx[] = { min(characters[0]->centerX + radius, (float)screenW-1), min(characters[1]->centerX + radius, (float)screenW-1) };
    float by[] = { min(characters[0]->centerY + radius, (float)screenH-1), min(characters[1]->centerY + radius, (float)screenH-1) };

    for(int i=0; i<numCores; i++) {
        //render_t(this, i, numCores, screenW, screenH, mw, mh, mx, my, ax, ay, bx, by);
        mapRenderThreads.push_back(std::thread(render_t, this, i, numCores, screenW, screenH, mw, mh, mx, my, ax, ay, bx, by));
    }

    for(int i=0; i<numCores; i++) {
        mapRenderThreads[i].join();
    }

    mapRenderThreads.clear();

}

void Map::render_t(Map *map, int threadNum, int numThreads, float screenW, float screenH, float mw, float mh, float mx, float my, float ax[], float ay[], float bx[], float by[]) {


    //if(map->debugTileRenderMode == 1) {

        int n = map->w*map->h;

        //int dx = 150 + 150 * cos(timer.totalTime/4.0);
        //int dy = 150 + 150 * sin(timer.totalTime/4.0);

        for(int j=threadNum; j<n; j+=numThreads) {
            int i = j*4;

            /*if(map->useDebugColors) {
                if(map->tiles[j].type == Map::Tile::None) {
                    map->tilePixels[i + 0] = map->noneR;
                    map->tilePixels[i + 1] = map->noneG;
                    map->tilePixels[i + 2] = map->noneB;
                    map->tilePixels[i + 3] = 0;
                }
                else if(map->tiles[j].type == Map::Tile::Ground) {
                    map->tilePixels[i + 0] = map->groundR;
                    map->tilePixels[i + 1] = map->groundG;
                    map->tilePixels[i + 2] = map->groundB;
                    map->tilePixels[i + 3] = map->tileAlpha;
                }
                else if(map->tiles[j].type == Map::Tile::Dirt) {
                    map->tilePixels[i + 0] = map->dirtR;
                    map->tilePixels[i + 1] = map->dirtG;
                    map->tilePixels[i + 2] = map->dirtB;
                    map->tilePixels[i + 3] = map->tileAlpha;
                }
                else if(map->tiles[j].type == Map::Tile::Water) {
                    map->tilePixels[i + 0] = map->waterR;
                    map->tilePixels[i + 1] = map->waterG;
                    map->tilePixels[i + 2] = map->waterB;
                    map->tilePixels[i + 3] = 150;
                }
                else if(map->tiles[j].type == Map::Tile::Oil) {
                    map->tilePixels[i + 0] = map->oilR;
                    map->tilePixels[i + 1] = map->oilG;
                    map->tilePixels[i + 2] = map->oilB;
                    map->tilePixels[i + 3] = 150;
                }
            }
            else {*/
                if(map->tiles[j].type == Map::Tile::None) {
                    /*int x = j % map->w + dx;
                    int y = j / map->w + dy;
                    int k = (x + y * map->bgPixels2W) * 4;
                    map->tilePixels[i + 0] = map->bgPixels2[k + 0];
                    map->tilePixels[i + 1] = map->bgPixels2[k + 1];
                    map->tilePixels[i + 2] = map->bgPixels2[k + 2];
                    map->tilePixels[i + 3] = map->bgPixels2[k + 3]; //250*/

                    map->tilePixels[i + 0] = map->bgPixels2[i + 0];
                    map->tilePixels[i + 1] = map->bgPixels2[i + 1];
                    map->tilePixels[i + 2] = map->bgPixels2[i + 2];
                    map->tilePixels[i + 3] = map->bgPixels2[i + 3]; //250
                    //map->tilePixels[i + 3] = 0;
                }
                else {
                    if(map->tiles[j].burning) {
                        map->tilePixels[i + 1] = randi2(0, 255);
                        map->tilePixels[i + 0] = randi2(map->tilePixels[i + 1], 255);
                        map->tilePixels[i + 2] = 0;
                        map->tilePixels[i + 3] = 255;
                    }
                    else {
                        map->tilePixels[i + 0] = map->tiles[j].color.r;
                        map->tilePixels[i + 1] = map->tiles[j].color.g;
                        map->tilePixels[i + 2] = map->tiles[j].color.b;
                        map->tilePixels[i + 3] = map->tiles[j].color.a;
                    }
                }
            //}

            /*
            if(map->fieldOfVision == Map::FieldOfVision::Halo) {
                float x = map->tileToWorldCoordinates[j].x;
                float y = map->tileToWorldCoordinates[j].y;
                bool isWithin[map->characters.size()];
                isWithin[0] = isWithinRect(x, y, ax[0], ay[0], bx[0], by[0]);
                isWithin[1] = isWithinRect(x, y, ax[1], ay[1], bx[1], by[1]);
                if(!isWithin[0] && !isWithin[1]) {
                    map->tilePixels[i + 0] = 0;
                    map->tilePixels[i + 1] = 0;
                    map->tilePixels[i + 2] = 0;
                    continue;
                }


                float t[map->characters.size()];
                for(int k=0; k<map->characters.size(); k++) {
                    t[k] = 1.0;
                    //isWithin[k] = false;
                }
                for(int k=0; k<map->characters.size(); k++) {

                    float dx = x - map->characters[k]->centerX;
                    float dy = y - map->characters[k]->centerY;
                    float d = (dx*dx + dy*dy) / (map->scaleX*map->scaleX);
                    d *= 0.0006;
                    d = d*d*d*d;

                    if(d < 1) {
                        d = 1;
                    }

                    t[k] = d;
                }
                if(isWithin[0] || isWithin[1]) {
                    float p = 1.0 / min(t[0], t[1]);
                    map->tilePixels[i + 0] *= p;
                    map->tilePixels[i + 1] *= p;
                    map->tilePixels[i + 2] *= p;
                }
                else if(isWithin[0] && isWithin[1]) {
                    float p = 1.0 / max(t[0], t[1]);
                    map->tilePixels[i + 0] *= p;
                    map->tilePixels[i + 1] *= p;
                    map->tilePixels[i + 2] *= p;
                }
                else {
                    map->tilePixels[i + 0] = 0;
                    map->tilePixels[i + 1] = 0;
                    map->tilePixels[i + 2] = 0;
                }
            }*/

        }





        /*tileImage.create(w, h, tilePixels);
        tileTexture.loadFromImage(tileImage);
        //tileSprite.setTexture(tileTexture, true);

        tileSprite.setPosition(mx, my);
        tileSprite.setScale(scaleX, scaleY);
        window.draw(tileSprite);*/
    //}

}





void Map::renderFinal(float screenW, float screenH, sf::RenderWindow &window) {
    float mw = w * scaleX;
    float mh = h * scaleY;
    float mx = screenW/2 - mw/2;
    float my = screenH/2 - mh/2;

    //renderBg(screenW, screenH, window);

    
    /*bgShaderRect.setPosition(mx + viewportX, my + viewportY);
    bgShaderRect.setScale(scaleX, scaleY);
    window.draw(bgShaderRect, &bgShader);*/

    /*bgSprite.setPosition(mx, my);
    bgSprite.setScale(scaleX, scaleY);
    bgShader.setUniform("timer", (float)timer.totalTime);
    window.draw(bgSprite, &bgShader);*/

    /*float t = timer.totalTime;
    float x = -20 + 20 * cos(t * 2.0*Pi / 5.0);
    float y = -20 + 20 * sin(t * 2.0*Pi / 5.0);

    bgSprite2.setPosition(mx+x, my+y);
    bgSprite2.setScale(scaleX, scaleY);
    window.draw(bgSprite2);*/


    // TODO there could be <num threads> tileImages and tileTextures...
    tileImage.create(w, h, tilePixels);
    tileTexture.loadFromImage(tileImage);
    //tileSprite.setTexture(tileTexture, true);

    tileSprite.setPosition(mx + viewportX, my + viewportY);
    tileSprite.setScale(scaleX, scaleY);
    window.draw(tileSprite);

    /*for(int i=0; i<largeTileSet01.largeTiles.size(); i++) {
        sf::Sprite *currentSprite = &largeTileSet01.largeTiles[i]->sprite;
        currentSprite->setPosition(w/3 + i*scaleX*32, h/2);
        currentSprite->setScale(scaleX, scaleY);
        window.draw(*currentSprite);
    }*/


    /*if(fieldOfVision == Map::FieldOfVision::Occlusion) {
        occlusionShader.setUniform("pixelTexture", tileTexture);
        occlusionShader.setUniform("occlusionTextureWidth", (float)occlusionRenderTextureW);
        for(int i=0; i<2; i++) {

            occlusionShader.setUniform("characterPos", sf::Glsl::Vec2(((int)characters[i]->x)/screenW, ((int)characters[i]->y)/screenH));
            occlusionRenderTexture[i].draw(tileSprite, &occlusionShader);
        }

        occlusionShaderPost.setUniform("occlusionTextureWidth", (float)occlusionRenderTextureW);
        for(int i=0; i<2; i++) {
            occlusionShaderPost.setUniform("occlusionTexture"+to_string(i), occlusionRenderTexture[i].getTexture());
            occlusionShaderPost.setUniform("characterPos"+to_string(i), sf::Glsl::Vec2(((int)characters[i]->x)/screenW, ((int)characters[i]->y)/screenH));
        }
        occlusionSpritePost.setPosition(mx, my);
        occlusionSpritePost.setScale(scaleX, scaleY);
        window.draw(occlusionSpritePost, &occlusionShaderPost);
    }*/

    /*for(int i=0; i<2; i++) {
        occlusionSpriteDebug.setTexture(occlusionRenderTexture[i].getTexture(), true);
        occlusionSpriteDebug.setPosition(mx+300, my+i*scaleY*10);
        occlusionSpriteDebug.setScale(scaleX, scaleY*10);
        window.draw(occlusionSpriteDebug);
    }*/
}
















void Map::asd(TilePainter &tilePainter, int threshold) {
    //printf("Threshold %d\n", threshold);

    for(int i=0; i<w*h; i++) {
        int c = landGrowerPixels[i*4+0];
        /*if(c < threshold) c = 255;
        else c = 0;
        bgPixels[i*4+0] = c;
        bgPixels[i*4+1] = c;
        bgPixels[i*4+2] = c;
        bgPixels[i*4+3] = landGrowerPixels[i*4+3];*/
        if(c == threshold) {
            int px = i % w * scaleX;
            int py = i / w * scaleY;
            bool characterOnTheWay = false;
            for(int k=0; k<characters.size(); k++) {
                /*float x = characters[k]->x + characters[k]->w*0.5;
                float y = characters[k]->y + characters[k]->h*0.5; TODO remove this */
                float x = characters[k]->x;
                float y = characters[k]->y;
                float r = max(characters[k]->w*scaleX, characters[k]->h*scaleY) * 2;
                float dx = x - px;
                float dy = y - py;
                float d = sqrt(dx*dx + dy*dy);
                if(d < r) {
                    characterOnTheWay = true;
                }
            }
            if(!characterOnTheWay) {
                paintTiles(tilePainter, px, py);
            }
        }
    }

    bgImage.create(w, h, bgPixels);
    bgTexture.loadFromImage(bgImage);
    //bgSprite.setTexture(bgTexture, true);
}






void Map::asd2(TilePainter &tilePainter, int threshold) {
    //printf("Threshold %d\n", threshold);

    for(int i=0; i<w*h; i++) {
        int c = landGrowerPixels[i*4+0];
        /*if(c < threshold) c = 255;
        else c = 0;
        bgPixels[i*4+0] = c;
        bgPixels[i*4+1] = c;
        bgPixels[i*4+2] = c;
        bgPixels[i*4+3] = landGrowerPixels[i*4+3];*/
        if(c >= threshold) {
            int px = i % w * scaleX;
            int py = i / w * scaleY;
            bool characterOnTheWay = false;
            for(int k=0; k<characters.size(); k++) {
                /*float x = characters[k]->x + characters[k]->w*0.5;
                float y = characters[k]->y + characters[k]->h*0.5; TODO remove this */
                float x = characters[k]->x;
                float y = characters[k]->y;

                float r = max(characters[k]->w*scaleX, characters[k]->h*scaleY) * 2;
                float dx = x - px;
                float dy = y - py;
                float d = sqrt(dx*dx + dy*dy);
                if(d < r) {
                    characterOnTheWay = true;
                }
            }
            if(!characterOnTheWay) {
                paintTiles(tilePainter, px, py);
            }
        }
    }

    bgImage.create(w, h, bgPixels);
    bgTexture.loadFromImage(bgImage);
    //bgSprite.setTexture(bgTexture, true);
}







void LandGrower::use(float x, float y, float vx, float vy, float angle) {
    active = true;
    /*if(itemUserCharacter->mana >= 100) {

    }*/
}



void LandGrower::asdLandGrower(Map &map, int threshold) {

    for(int i=0; i<map.w*map.h; i++) {
        int c = map.landGrowerPixels[i*4+0];

        if(c == threshold) {
            int px = i % map.w * scaleX;
            int py = i / map.w * scaleY;
            int tx = i % map.w;
            int ty = i / map.w;
            bool characterOnTheWay = false;
            for(int k=0; k<map.characters.size(); k++) {
                float x = map.characters[k]->x;
                float y = map.characters[k]->y;
                float r = max(map.characters[k]->w*scaleX, map.characters[k]->h*scaleY) * 2;
                float dx = x - px;
                float dy = y - py;
                float d = sqrt(dx*dx + dy*dy);
                if(d < r) {
                    characterOnTheWay = true;
                }
            }

            bool vehicleOnTheWay = false;
            for(int k=0; k<vehicles.size(); k++) {
                float x = vehicles[k]->x;
                float y = vehicles[k]->y;
                float r = max(vehicles[k]->w*scaleX, vehicles[k]->h*scaleY) * 2;
                float dx = x - px;
                float dy = y - py;
                float d = sqrt(dx*dx + dy*dy);
                if(d < r) {
                    vehicleOnTheWay = true;
                }
            }

            //if(!characterOnTheWay) {
            //    map.paintTiles(tilePainter, px, py);
            //}
            //if(!characterOnTheWay && (map.tiles[i].type != tilePainter.type || map.tiles[i].flammable != tilePainter.flammable)) {
            if(!characterOnTheWay && !vehicleOnTheWay && map.tiles[i].type == Map::Tile::Type::None) {

                map.paintTilesNoMapping(tilePainter, tx, ty);   // TODO check this!

                map.tiles[i].health = randf(1, 8);

                if(randf(0, 1) < 2.0/3.0) {
                //{
                //for(int p=0; p<2; p++) {
                    LandGrowerProjectile *landGrowerProjectile = new LandGrowerProjectile();
                    float angle = randf(0, 2.0*Pi);
                    //float angle = -0.5*Pi;
                    float v = randf(10, 70);
                    landGrowerProjectile->vx = v * cos(angle);
                    landGrowerProjectile->vy = v * sin(angle);
                    //float angle2 = randf(0, 2.0*Pi);
                    //float rf = randf(0.5, 1);
                    landGrowerProjectile->x = px;
                    landGrowerProjectile->y = py;
                    landGrowerProjectile->gravityHasAnEffect = false;

                    if(rock) {
                        landGrowerProjectile->color = map.getGroundTileColor();
                    }
                    else {
                        landGrowerProjectile->color = map.getFlammableGroundTileColor();
                    }
                    //landGrowerProjectile->color = sf::Color(r, g, b, 150);
                    landGrowerProjectile->color2 = landGrowerProjectile->color;
                    landGrowerProjectile->color2.a = 0;
                    landGrowerProjectile->duration = randf(0.25, 0.5);
                    //landGrowerProjectile->characters = characters;
                    landGrowerProjectile->projectileUserCharacter = itemUserCharacter;
                    landGrowerProjectile->projectileUserVehicle = itemUserVehicle;
                    projectiles.push_back(landGrowerProjectile);
                }
            }
        }
    }

    //map.bgImage.create(map.w, map.h, map.bgPixels);   // TODO check this!
    //map.bgTexture.loadFromImage(map.bgImage);
    //map.bgSprite.setTexture(map.bgTexture, true);
}







void LandGrower::asdLandGrowerTiles(Map &map, int threshold) {

    //for(int i=0; i<map.w*map.h; i++) {
    int tileSize = 32;
    for(int tx=0; tx<map.w; tx+=tileSize) {
        for(int ty=0; ty<map.h; ty+=tileSize) {
            int i = tx + ty*map.w;

            int c = map.landGrowerPixels[i*4+0];

            if(c == threshold) {
                int px = tx * scaleX;
                int py = ty * scaleY;
                
                bool characterOnTheWay = false;
                for(int k=0; k<map.characters.size(); k++) {
                    float x = map.characters[k]->x;
                    float y = map.characters[k]->y;
                    float r = max(map.characters[k]->w*scaleX, map.characters[k]->h*scaleY) * 2;
                    float dx = x - px;
                    float dy = y - py;
                    float d = sqrt(dx*dx + dy*dy);
                    if(d < r) {
                        characterOnTheWay = true;
                    }
                }

                bool vehicleOnTheWay = false;
                for(int k=0; k<vehicles.size(); k++) {
                    float x = vehicles[k]->x;
                    float y = vehicles[k]->y;
                    float r = max(vehicles[k]->w*scaleX, vehicles[k]->h*scaleY) * 2;
                    float dx = x - px;
                    float dy = y - py;
                    float d = sqrt(dx*dx + dy*dy);
                    if(d < r) {
                        vehicleOnTheWay = true;
                    }
                }

                if(!characterOnTheWay && !vehicleOnTheWay && map.tiles[i].type == Map::Tile::Type::None) {

                    int largeTileIndex = 0;
                    int kx = tx / 32;
                    int ky = ty / 32;
                    if(rock) {
                        if((kx+ky) % 2 == 0) {
                            largeTileIndex = 5;
                        }
                        else {
                            largeTileIndex = 6;
                        }
                        float r = randf2(0, 1);
                        if(r < 0.1) {
                            largeTileIndex = 0;
                        }
                        else if(r < 0.2) {
                            largeTileIndex = 1;
                        }
                        else if(r < 0.3) {
                            largeTileIndex = 4;
                        }
                    }
                    else {
                        if((kx+ky) % 2 == 0) {
                            largeTileIndex = 0;
                        }
                        else {
                            largeTileIndex = 1;
                        }
                        float r = randf2(0, 1);
                        if(r < 0.1) {
                            largeTileIndex = 2;
                        }
                        else if(r < 0.2) {
                            largeTileIndex = 3;
                        }
                        else if(r < 0.3) {
                            largeTileIndex = 4;
                        }
                    }

                    for(int i=0; i<32; i++) {
                        for(int j=0; j<32; j++) {
                            
                            if(tx + i >= map.w || ty+j >= map.h){                                
                                continue;
                            }
                            if(rock) {
                                map.paintTilesNoMappingLargeTiles(map.largeTileSet01, largeTileIndex, false, tx+i, ty+j, i, j);
                            }
                            else {
                                map.paintTilesNoMappingLargeTiles(map.largeTileSet02, largeTileIndex, true, tx+i, ty+j, i, j);
                            }
                            

                            if(i == 0 || j == 0 || i == 31 || j == 31) {
                                for(int k=0; k<5; k++) {
                                    LandGrowerProjectile *p = new LandGrowerProjectile();
                                    float angle = randf(0, 2.0*Pi);
                                    //float angle = -0.5*Pi;
                                    float v = randf(10, 140);
                                    p->vx = v * cos(angle);
                                    p->vy = v * sin(angle);
                                    //float angle2 = randf(0, 2.0*Pi);
                                    //float rf = randf(0.5, 1);
                                    p->x = px + i * scaleX;
                                    p->y = py + j * scaleY;
                                    p->gravityHasAnEffect = false;

                                    if(rock) {
                                        p->color = map.getGroundTileColor();
                                    }
                                    else {
                                        p->color = map.getFlammableGroundTileColor();
                                    }
                                    //landGrowerProjectile->color = sf::Color(r, g, b, 150);
                                    p->color2 = p->color;
                                    p->color2.a = 0;
                                    p->duration = randf(0.25, 0.5);
                                    //landGrowerProjectile->characters = characters;
                                    p->projectileUserCharacter = itemUserCharacter;
                                    p->projectileUserVehicle = itemUserVehicle;
                                    projectiles.push_back(p);
                                }
                            }
                        }
                    }

                }
            }
        }
    }

    //map.bgImage.create(map.w, map.h, map.bgPixels);   // TODO check this!
    //map.bgTexture.loadFromImage(map.bgImage);
    //map.bgSprite.setTexture(map.bgTexture, true);
}




















void Projectile::checkInitialSelfCollision() {
    if(!projectileUserCharacter && !projectileUserVehicle) {
        printf("at Projectile::isInitialSelfCollision(): projectileUserCharacter == nullptr && projectileUserVehicle == nullptr\n");
        initialSelfCollision = false;
        return;
    }
    else if(initialSelfCollision) {
        if(projectileUserCharacter) {
            float ax = projectileUserCharacter->x -     projectileUserCharacter->w*projectileUserCharacter->scaleX * 0.5;
            float ay = projectileUserCharacter->y - projectileUserCharacter->h*projectileUserCharacter->scaleY * 0.5;
            float bx = projectileUserCharacter->x + projectileUserCharacter->w*projectileUserCharacter->scaleX * 0.5;
            float by = projectileUserCharacter->y + projectileUserCharacter->h*projectileUserCharacter->scaleY * 0.5;

            if(!isWithinRect(x, y, ax, ay, bx, by)) {
                initialSelfCollision = false;
            }
        }
        else {
            float ax = projectileUserVehicle->x -     projectileUserVehicle->w*projectileUserVehicle->scaleX * 0.5;
            float ay = projectileUserVehicle->y - projectileUserVehicle->h*projectileUserVehicle->scaleY * 0.5;
            float bx = projectileUserVehicle->x + projectileUserVehicle->w*projectileUserVehicle->scaleX * 0.5;
            float by = projectileUserVehicle->y + projectileUserVehicle->h*projectileUserVehicle->scaleY * 0.5;

            if(!isWithinRect(x, y, ax, ay, bx, by)) {
                initialSelfCollision = false;
            }
        }
    }
}




void Bomb::use(float x, float y, float vx, float vy, float angle) {

    BombProjectile *bombProjectile = new BombProjectile();
    float m = 0;
    if(itemUserCharacter) {
        m = max(itemUserCharacter->w*itemUserCharacter->scaleX, itemUserCharacter->h*itemUserCharacter->scaleY) * 0.5;
    }
    else if(itemUserVehicle) {
        m = max(itemUserVehicle->w*itemUserVehicle->scaleX, itemUserVehicle->h*itemUserVehicle->scaleY) * 0.5;
    }

    bombProjectile->x = x + m * cos(angle);
    bombProjectile->y = y + m * sin(angle);
    bombProjectile->vx = /*vx +*/ throwingVelocity * cos(angle);
    bombProjectile->vy = /*vy +*/ throwingVelocity * sin(angle);
    bombProjectile->gravityHasAnEffect = true;
    bombProjectile->characters = characters;
    bombProjectile->projectileUserCharacter = itemUserCharacter;
    bombProjectile->projectileUserVehicle = itemUserVehicle;
    projectiles.push_back(bombProjectile);
}




bool Bomb::BombProjectile::_update(Map& map, float dt, bool exploded) {


    if(exploded) return exploded;

    //float m = max(vx * dt, vy * dt);
    /*if(m > 1) {
        printf("m %f\n", m);
    }*/

    checkInitialSelfCollision();

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter && initialSelfCollision) continue;

        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY * 0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY * 0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            createExplosion(map, x, y, characters, 100, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
        }*/
        if(map.characters[i]->intersects(x, y)) {
            createExplosion(map, x, y, characters, 100, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle && initialSelfCollision) continue;
        int ix = 0, iy = 0;
        if(vehicles[i]->checkPixelPixelCollision(x, y, ix, iy)) {
            createExplosion(map, x, y, characters, 100, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    bool collided = map.checkCollision(x, y);
    if(collided) {
        createExplosion(map, x, y, characters, 100, 1000, 0, 200);
        exploded = true;
        canBeRemoved = true;
    }

    return exploded;
}









bool ClusterBomb::ClusterProjectile::_update(Map& map, float dt, bool exploded) {


    if(exploded) return exploded;

    //float m = max(vx * dt, vy * dt);
    /*if(m > 1) {
        printf("m %f\n", m);
    }*/

    checkInitialSelfCollision();

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter && initialSelfCollision) continue;

        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY * 0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY * 0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            createExplosion(map, x, y, characters, 40, 500, 0, 200);
            exploded = true;
            canBeRemoved = true;
        }*/
        if(map.characters[i]->intersects(x, y)) {
            createExplosion(map, x, y, characters, 40, 250, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle && initialSelfCollision) continue;

        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            createExplosion(map, x, y, characters, 40, 250, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }


    bool collided = map.checkCollision(x, y);
    if(collided) {
        createExplosion(map, x, y, characters, 40, 250, 0, 200);
        exploded = true;
        canBeRemoved = true;
    }

    return exploded;
}














void Flame::update(Map& map, float dt) {
    if(gravityHasAnEffect) {
        vy += gravity * dt;
    }
    x += vx * dt;
    y += vy * dt;

    time += dt;

    if(time >= duration) {
        canBeRemoved = true;
        return;
    }
    if(dealDamagePerSecond > 0) {
        for(int i=0; i<map.characters.size(); i++) {
            if(map.characters[i]->intersects(x, y)) {
                map.characters[i]->takeDamage(dealDamagePerSecond * dt, 1.5, 0.15, 0, 0);
            }
        } // TODO check this out!

        for(int i=0; i<vehicles.size(); i++) {
            if(vehicles[i]->checkPixelPixelCollision(x, y)) {
                vehicles[i]->takeDamage(dealDamagePerSecond * dt);
            }
        }
    }
}


















void FireProjectile::update(Map& map, float dt) {
    if(gravityHasAnEffect) {
        vy += gravity * dt;
    }
    vx = vx * randf(airResistanceMin, airResistanceMax);
    vy = vy * randf(airResistanceMin, airResistanceMax);
    vy -= randf(risingVelocityMin, risingVelocityMax) * dt;

    x += vx * dt;
    y += vy * dt;

    time += dt;

    if(time >= duration+duration2) {
        canBeRemoved = true;
        return;
    }

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter) continue;
        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY * 0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY * 0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            map.characters[i]->takeDamage(dt * damage, 1.5, 0.15, 0, 0);
            canBeRemoved = true;
        }*/

        if(map.characters[i]->intersects(x, y)) {
            map.characters[i]->takeDamage(dt * damage, 1.5, 0.15, 0, 0);
            canBeRemoved = true;
            return; // TODO check this!
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle) continue;

        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            vehicles[i]->takeDamage(dt * damage);
            canBeRemoved = true;
            return; // TODO check this!
        }
    }


    int px = map.mapX(x, screenW);
    int py = map.mapY(y, screenH);
    if(px != -1 && py != -1) {

        if(map.tiles[px + py*map.w].type != Map::Tile::None) {
            createSmoke(x, y, map.characters, 1, 20, 200);
            if(map.tiles[px + py*map.w].flammable && !map.tiles[px + py*map.w].burning) {
                map.tiles[px + py*map.w].burning = true;
            }

            canBeRemoved = true;
        }
    }

}













void LightningStrikeProjectile::update(Map &map, float dt) {
    if(gravityHasAnEffect) {
        vy += gravity * dt;
    }
    if(vx == 0 && vy == 0 && time > 0.333*duration) {
        float a = randf(0, 2.0*Pi);
        float v = randf(0, 10);
        vx = v * cos(a);
        vy = v * sin(a);
    }

    x += vx * dt;
    y += vy * dt;

    time += dt;

    if(time >= duration) {
        canBeRemoved = true;
        return;
    }

    /*for(int i=0; i<characters.size(); i++) {
        if(characters[i] == projectileUserCharacter) {
            continue;
        }
        float ax = characters[i]->x;
        float ay = characters[i]->y;
        float bx = characters[i]->x + characters[i]->w*characters[i]->scaleX;
        float by = characters[i]->y + characters[i]->h*characters[i]->scaleY;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            characters[i]->takeDamage(dt*10, 1.5, 0.5, 0, 0);
        }
    }*/
}






void LightningStrikeProjectile::createLigningTrail(Map &map, LightningStrikeProjectile *lightningStrikeProjectilePrev, int numBranches) {
    //if(n == 0) return;


    currentAngle = lightningStrikeProjectilePrev->currentAngle;
    initialAngle = lightningStrikeProjectilePrev->initialAngle;

    //currentAngle += randf(angleChangeMin, angleChangeMax);
    float t = randf(0, 1);
    if(t < 0.01) {
        currentAngle += angleChangeMin;
    }
    else if(t < 0.02) {
        currentAngle += angleChangeMax;
    }

    if(currentAngle < initialAngle + angleMin) {
        currentAngle = initialAngle + angleMin;
    }
    if(currentAngle > initialAngle + angleMax) {
        currentAngle = initialAngle + angleMin;
    }

    x = lightningStrikeProjectilePrev->x + cos(currentAngle) * 1.0;    // TODO fix the distance
    y = lightningStrikeProjectilePrev->y + sin(currentAngle) * 1.0;

    vx = 0;
    vy = 0;

    gravityHasAnEffect = false;
    repellerHasAnEffect = false;

    color = sf::Color(255, 255, 255, randi(10, 150));
    color2 = sf::Color(255, 255, 255, 0);

    //duration = lightningStrikeProjectilePrev->duration;

    duration = randf(1, 3);

    //fireProjectile->duration2 = randf(0.1, 0.3);

    //characters = lightningStrikeProjectilePrev->characters;
    projectileUserCharacter = lightningStrikeProjectilePrev->projectileUserCharacter;
    projectileUserVehicle = lightningStrikeProjectilePrev->projectileUserVehicle;


    projectiles.push_back(this);

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter) {
            continue;
        }

        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY * 0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY * 0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            map.characters[i]->takeDamage(1.5, 1, 1, 0, 0);
        }*/

        if(map.characters[i]->intersects(x, y)) {
            map.characters[i]->takeDamage(1.5, 1, 1, 0, 0);
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle) {
            continue;
        }
        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            vehicles[i]->takeDamage(1.5);

            createExplosion(map, x, y, characters, 25, 10, 10, 150);
        }
    }

    bool continueTrail = true;

    bool collided = map.checkCollision(x, y, Map::CollisionTileType::All);
    if(collided) {
        continueTrail = false;
    }

    if(continueTrail) {
        LightningStrikeProjectile *lightningStrikeProjectile = new LightningStrikeProjectile();
        lightningStrikeProjectile->createLigningTrail(map, this, numBranches);

        bool branch = randf(0.0, 1.0) < probabilityOfBranching;
        if(branch && numBranches < 2) {
            LightningStrikeProjectile *lightningStrikeProjectile = new LightningStrikeProjectile();
            lightningStrikeProjectile->createLigningTrail(map, this, numBranches+1);
        }
    }
    else {
        createExplosion(map, x, y, characters, 25, 500, 10, 150);
    }
}






void LightningStrike::use(float x, float y, float vx, float vy, float angle) {
    initialLightningStrikeProjectile = new LightningStrikeProjectile();
    /*initialLightningStrikeProjectile->x = itemUserCharacter->x + itemUserCharacter->w*scaleX*0.5;
    initialLightningStrikeProjectile->y = itemUserCharacter->y + itemUserCharacter->h*scaleY*0.5; TODO remove this */

    if(itemUserCharacter) {
        initialLightningStrikeProjectile->x = itemUserCharacter->x;
        initialLightningStrikeProjectile->y = itemUserCharacter->y;

        initialLightningStrikeProjectile->initialAngle = itemUserCharacter->globalAngle;
        initialLightningStrikeProjectile->currentAngle = itemUserCharacter->globalAngle;
    }
    else if(itemUserVehicle) {
        initialLightningStrikeProjectile->x = itemUserVehicle->x;
        initialLightningStrikeProjectile->y = itemUserVehicle->y;

        initialLightningStrikeProjectile->initialAngle = itemUserVehicle->globalAngle;
        initialLightningStrikeProjectile->currentAngle = itemUserVehicle->globalAngle;

    }
    initialLightningStrikeProjectile->vx = 0;
    initialLightningStrikeProjectile->vy = 0;


    initialLightningStrikeProjectile->gravityHasAnEffect = false;
    initialLightningStrikeProjectile->repellerHasAnEffect = false;

    initialLightningStrikeProjectile->color = sf::Color(255, 255, 255, randi(100, 255));
    initialLightningStrikeProjectile->color2 = sf::Color(255, 255, 255, 0);

    initialLightningStrikeProjectile->duration = 1;

    //initialLightningStrikeProjectile->duration2 = randf(0.1, 0.3);

    //initialLightningStrikeProjectile->characters = characters;
    initialLightningStrikeProjectile->projectileUserCharacter = itemUserCharacter;
    initialLightningStrikeProjectile->projectileUserVehicle = itemUserVehicle;
    projectiles.push_back(initialLightningStrikeProjectile);

    readyToStrike = true;
}



















void MissileLauncher::use(float x, float y, float vx, float vy, float angle) {

    MissileProjectile *missileProjectile = new MissileProjectile();
    float m = 0;

    if(itemUserCharacter) {
        m = max(itemUserCharacter->w*itemUserCharacter->scaleX, itemUserCharacter->h*itemUserCharacter->scaleY) * 0.5;
    }
    else if(itemUserVehicle) {
        m = max(itemUserVehicle->w*itemUserVehicle->scaleX, itemUserVehicle->h*itemUserVehicle->scaleY) * 0.5;
    }

    missileProjectile->x = x + m * cos(angle);
    missileProjectile->y = y + m * sin(angle);
    missileProjectile->angle = angle;
    missileProjectile->vx = missileInitialVelocity * cos(angle);
    missileProjectile->vy = missileInitialVelocity * sin(angle);
    missileProjectile->missileAcceleration = missileAcceleration;
    missileProjectile->gravityHasAnEffect = false;
    //missileProjectile->characters = characters;
    missileProjectile->projectileUserCharacter = itemUserCharacter;
    missileProjectile->projectileUserVehicle = itemUserVehicle;

    missileProjectile->flameSoundInstance = soundWrapper.playSoundBuffer(missileProjectile->flameSoundBuffer, true, 20);
    projectiles.push_back(missileProjectile);
}




bool MissileLauncher::MissileProjectile::_update(Map& map, float dt, bool exploded) {


    if(exploded) return exploded;


    //checkInitialSelfCollision();

    for(int i=0; i<map.characters.size(); i++) {
        //if(map.characters[i] == projectileUserCharacter && initialSelfCollision) continue;
        if(map.characters[i] == projectileUserCharacter) continue;
        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY * 0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY * 0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            createExplosion(map, x, y, characters, 150, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
        }*/

        if(map.characters[i]->intersects(x, y)) {
            createExplosion(map, x, y, characters, 150, 2000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle) {
            continue;
        }
        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            createExplosion(map, x, y, characters, 150, 2000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    bool collided = map.checkCollision(x, y);
    if(collided) {
        createExplosion(map, x, y, characters, 150, 2000, 0, 200);
        exploded = true;
        canBeRemoved = true;
    }

    return exploded;
}






















void Blaster::use(float x, float y, float vx, float vy, float angle){

    float dx = cos(angle);
    float dy = sin(angle);

    if(useOneBoltOnly) {
        BlasterProjectile *blasterProjectile = new BlasterProjectile();

        blasterProjectile->x = x + 10 * dx;
        blasterProjectile->y = y + 10 * dy;
        blasterProjectile->vx = speed * dx;
        blasterProjectile->vy = speed * dy;
        blasterProjectile->gravityHasAnEffect = false;
        //blasterProjectile->repellerHasAnEffect = false;
        blasterProjectile->color = sf::Color(255, 0, 0, 200);
        blasterProjectile->numBlasterProjectiles = numBlasterProjectiles;
        blasterProjectile->useOneBoltOnly = useOneBoltOnly;
        blasterProjectile->angleOneBoltOnly = angle;
        //blasterProjectile->characters = characters;
        blasterProjectile->projectileUserCharacter = itemUserCharacter;
        blasterProjectile->projectileUserVehicle = itemUserVehicle;
        blasterProjectile->team = team;
        blasterProjectile->ignoreTeamMates = ignoreTeamMates;
        projectiles.push_back(blasterProjectile);
    }
    else {
        for(int i=0; i<numBlasterProjectiles; i++) {
            BlasterProjectile *blasterProjectile = new BlasterProjectile();

            blasterProjectile->x = x + (i+10) * dx;
            blasterProjectile->y = y + (i+10) * dy;
            blasterProjectile->vx = speed * dx;
            blasterProjectile->vy = speed * dy;
            blasterProjectile->gravityHasAnEffect = false;
            //blasterProjectile->repellerHasAnEffect = false;
            blasterProjectile->color = sf::Color(255, 0, 0, 200);
            blasterProjectile->numBlasterProjectiles = numBlasterProjectiles;
            blasterProjectile->useOneBoltOnly = useOneBoltOnly;
            //blasterProjectile->characters = characters;
            blasterProjectile->projectileUserCharacter = itemUserCharacter;
            blasterProjectile->projectileUserVehicle = itemUserVehicle;
            blasterProjectile->team = team;
            blasterProjectile->ignoreTeamMates = ignoreTeamMates;
            projectiles.push_back(blasterProjectile);
        }
    }
    soundWrapper.playSoundBuffer(blasterSoundBuffer, false, 30);
}









bool Blaster::BlasterProjectile::_update(Map& map, float dt, bool hit) {
    if(hit) return hit;

    //checkInitialSelfCollision();

    float damage = 0;
    if(useOneBoltOnly) {
        damage = 10;
    }
    else {
        damage = 10.0/numBlasterProjectiles;
    }

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter) continue;

        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY * 0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY * 0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            map.characters[i]->takeDamage(damage, 1, 1, 0, 0);
            createExplosion(map, x, y, characters, 5, 1, 0, 100);
            hit = true;
            canBeRemoved = true;
        }*/
        if(map.characters[i]->intersects(x, y)) {
            map.characters[i]->takeDamage(damage, 1, 1, 0, 0);
            createExplosion(map, x, y, characters, 5, 1, 0, 100);
            hit = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle) {
            continue;
        }
        if(ignoreTeamMates && vehicles[i]->team == team) {
            continue;
        }
        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            vehicles[i]->takeDamage(damage);
            createExplosion(map, x, y, characters, 5, 1, 0, 100);
            hit = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    bool collided = map.checkCollision(x, y);
    if(collided) {
        createExplosion(map, x, y, characters, 1, 2, 0, 100);
        hit = true;
        canBeRemoved = true;
    }


    return hit;
}


















bool Shotgun::ShotgunProjectile::_update(Map& map, float dt, bool hit) {
    if(hit) return hit;

    checkInitialSelfCollision();

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter && initialSelfCollision) {
            continue;
        }

        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY * 0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY * 0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            map.characters[i]->takeDamage(100.0 / numShotgunProjectiles, 1, 1, 0, 0);
            hit = true;
            canBeRemoved = true;
        }*/
        if(map.characters[i]->intersects(x, y)) {
            map.characters[i]->takeDamage(100.0 / numShotgunProjectiles, 1, 1, 0, 0);
            hit = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle && initialSelfCollision) {
            continue;
        }

        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            vehicles[i]->takeDamage(25.0 / numShotgunProjectiles);
            hit = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }


    bool collided = map.checkCollision(x, y);
    if(collided) {
        createExplosion(map, x, y, characters, 1, 2, 0, 100);
        hit = true;
        canBeRemoved = true;
    }


    return hit;
}




void Shotgun::use(float x, float y, float vx, float vy, float angle) {

    for(int i=0; i<numShotgunProjectiles; i++) {
        ShotgunProjectile *shotgunProjectile = new ShotgunProjectile();

        float k = 0;

        if(itemUserCharacter) {
            k = max(itemUserCharacter->w*scaleX, itemUserCharacter->h*scaleY) * 0.5;
        }
        else if(itemUserVehicle) {
            k = max(itemUserVehicle->w*scaleX, itemUserVehicle->h*scaleY) * 0.5;
        }
        float a = angle + randf(-deltaAngle, deltaAngle);
        float speed = randf(speedMin, speedMax);

        shotgunProjectile->x = x + k * cos(a);
        shotgunProjectile->y = y + k * sin(a);
        shotgunProjectile->vx = speed * cos(a);
        shotgunProjectile->vy = speed * sin(a);
        shotgunProjectile->numShotgunProjectiles = numShotgunProjectiles;
        shotgunProjectile->gravityHasAnEffect = true;
        shotgunProjectile->repellerHasAnEffect = true;
        shotgunProjectile->color = sf::Color(255, 255, 255, 150);
        //shotgunProjectile->characters = characters;
        shotgunProjectile->projectileUserCharacter = itemUserCharacter;
        shotgunProjectile->projectileUserVehicle = itemUserVehicle;
        projectiles.push_back(shotgunProjectile);

    }
    soundWrapper.playSoundBuffer(shotgunSoundBuffer, false, 50);
}















void Bomb::BombProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, /*ExplosionProjectile::Direction dir,*/ float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer);

    for(int i=0; i<map.characters.size(); i++) {
        float dx = map.characters[i]->x - x;
        float dy = map.characters[i]->y - y;

        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            map.characters[i]->takeDamage(25, 1, 1, 0, 0);
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->checkCirclePixelCollision(x, y, explosionRadius)) {
            vehicles[i]->takeDamage(25);
        }
    }

    float rr = explosionRadius * explosionRadius;
    float rrPlus1 = (explosionRadius+1) * (explosionRadius+1);

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius-1; i<explosionRadius+1; i++) {
        for(int j=-explosionRadius-1; j<explosionRadius+1; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
            else if(trr < rrPlus1) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                }
            }
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    /*for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(explosionRadius-ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 1.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }*/

    createExplosion2(x, y, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);

}



















void ClusterBomb::ClusterProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, /*ExplosionProjectile::Direction dir,*/ float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer);

    for(int i=0; i<map.characters.size(); i++) {
        float dx = map.characters[i]->x - x;
        float dy = map.characters[i]->y - y;

        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            map.characters[i]->takeDamage(100.0/8.0, 1, 1, 0, 0);
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->checkCirclePixelCollision(x, y, explosionRadius)) {
            vehicles[i]->takeDamage(100.0/8.0);
        }
    }

    float rr = explosionRadius * explosionRadius;

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius; i<explosionRadius; i++) {
        for(int j=-explosionRadius; j<explosionRadius; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    /*for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 0.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }*/


    createExplosion2(x, y, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);

}

















void LightningStrikeProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {



    /*for(int i=0; i<characters.size(); i++) {
        float dx = characters[i]->x + characters[i]->w*0.5*characters[i]->scaleX - x;
        float dy = characters[i]->y + characters[i]->h*0.5*characters[i]->scaleY - y;


        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            characters[i]->takeDamage(25, 1, 1, dx/dist, dy/dist);
        }
    }*/

    float rr = explosionRadius * explosionRadius;
    float rrPlus1 = (explosionRadius+1) * (explosionRadius+1);

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius-1; i<explosionRadius+1; i++) {
        for(int j=-explosionRadius-1; j<explosionRadius+1; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
            else if(trr < rrPlus1) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                }
            }
        }
    }

    /*for(int i=-explosionRadius; i<explosionRadius; i++) {
        for(int j=-explosionRadius; j<explosionRadius; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
        }
    }*/



    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        /*int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);*/
        explosionProjectile->color = sf::Color(255, 255, 255, randi(0, 255));
        explosionProjectile->color2 = sf::Color(255, 255, 255, 0);

        explosionProjectile->repellerHasAnEffect = false;

        explosionProjectile->duration = 0.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }

}
















void Blaster::BlasterProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, /*ExplosionProjectile::Direction dir,*/ float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    /*for(int i=0; i<characters.size(); i++) {
        float dx = characters[i]->x + characters[i]->w*0.5*characters[i]->scaleX - x;
        float dy = characters[i]->y + characters[i]->h*0.5*characters[i]->scaleY - y;


        float dist = sqrt(dx*dx + dy*dy);
        float d = max(characters[i]->w*characters[i]->scaleX, characters[i]->h*characters[i]->scaleY);

        if(dist <= d) {
            characters[i]->takeDamage(10.0/numBlasterProjectiles, 1, 1, dx/dist, dy/dist);
        }
    }*/
/*
    float rr = explosionRadius * explosionRadius;

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius; i<explosionRadius; i++) {
        for(int j=-explosionRadius; j<explosionRadius; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    //if(map.tiles[tx + ty*map.w].flammable) {
                    //    map.tiles[tx + ty*map.w].burning = true;
                    //}
                    //explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }*/
//numExplosionProjectiles = 0;
    if(useOneBoltOnly) {
        numExplosionProjectiles *= numBlasterProjectiles;
    }
    for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, 20);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        //int r = randi(100, 255);
        //int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        //g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(255, 0, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 0.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }

}















void NuclearBomb::NuclearBombProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    map.startEarthquake(Map::EarthquakeType::earthquakeNuclearBomb);

    soundWrapper.playSoundBuffer(explosionSoundBuffer);

    for(int i=0; i<map.characters.size(); i++) {
        float dx = map.characters[i]->x - x;
        float dy = map.characters[i]->y - y;

        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            map.characters[i]->takeDamage(100, 1, 1, 0, 0);
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->checkCirclePixelCollision(x, y, explosionRadius)) {
            vehicles[i]->takeDamage(100);
        }
    }

    float rr = explosionRadius * explosionRadius;

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius; i<explosionRadius; i++) {
        for(int j=-explosionRadius; j<explosionRadius; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    /*if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }*/
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
        }
    }
    //int numExplodedTiles = min(explodedTiles.size(), 5000);
    int numExplodedTiles = min(explodedTiles.size(), 500);
    for(int i=0; i<numExplodedTiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle2);
        explosionProjectile->y = y + r * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        //if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
        //    explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //}
        explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Oil;
        explosionProjectile->flyingTile.tile.health = randf(1, 4);
        explosionProjectile->flyingTile.tile.flammable = true;
        explosionProjectile->flyingTile.tile.burning = true;
        explosionProjectile->flyingTile.tile.color = map.getOilTileColor();

        int cr = randi(100, 255);
        int cg = (int)mapf(r, 0, explosionRadius, 0, randi(0, 255));
        cg = min(cg, cr);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(cr, cg, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->duration = randf(0.5, 1.5);
        explosionProjectile->duration = randf(1.5, 5);
        explosionProjectile->isNuclear = true;
        explosionProjectile->createSmokeTrail = randf(0, 1) < 0.01;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    int n = max(numExplosionProjectiles-numExplodedTiles, 0);

    /*for(int i=0; i<n; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = randf(0.5, 1.5);
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        explosionProjectile->createSmokeTrail = randf(0, 1) < 0.02;
        explosionProjectile->isNuclear = true;
        projectiles.push_back(explosionProjectile);
    }*/

    //createExplosion2(x, y, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);


    for(int i=0; i<n/35; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = randf(0.5, 2.5);
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        //explosionProjectile->createSmokeTrail = randf(0, 1) < 0.02;
        explosionProjectile->createSmokeTrail = true;
        explosionProjectile->isNuclear = true;
        projectiles.push_back(explosionProjectile);
    }

    
}

















void MissileLauncher::MissileProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer);

    flameSoundInstance->removeWithFadeOut(1.0);
    flameSoundInstance = nullptr; // TODO check this!

    for(int i=0; i<map.characters.size(); i++) {
        float dx = map.characters[i]->x - x;
        float dy = map.characters[i]->y - y;

        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            map.characters[i]->takeDamage(25, 1, 1, 0, 0);
        }
    }
    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->checkCirclePixelCollision(x, y, explosionRadius)) {
            vehicles[i]->takeDamage(25);
        }
    }

    float rr = explosionRadius * explosionRadius;
    float rrPlus1 = (explosionRadius+1) * (explosionRadius+1);

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius-1; i<explosionRadius+1; i++) {
        for(int j=-explosionRadius-1; j<explosionRadius+1; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
            else if(trr < rrPlus1) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                }
            }
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    /*for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 1.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }*/

    createExplosion2(x, y, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);
}
















void BouncyBomb::BouncyBombProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, /*ExplosionProjectile::Direction dir,*/ float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer);

    for(int i=0; i<map.characters.size(); i++) {
        float dx = map.characters[i]->x - x;
        float dy = map.characters[i]->y - y;

        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            map.characters[i]->takeDamage(15, 1, 1, 0, 0);
        }
    }
    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->checkCirclePixelCollision(x, y, explosionRadius)) {
            vehicles[i]->takeDamage(15);
        }
    }

    float rr = explosionRadius * explosionRadius;

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius; i<explosionRadius; i++) {
        for(int j=-explosionRadius; j<explosionRadius; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    /*for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 1.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }*/


    createExplosion2(x, y, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);
}












void Shotgun::ShotgunProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, /*ExplosionProjectile::Direction dir,*/ float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    /*for(int i=0; i<characters.size(); i++) {
        float dx = characters[i]->x + characters[i]->w*0.5*characters[i]->scaleX - x;
        float dy = characters[i]->y + characters[i]->h*0.5*characters[i]->scaleY - y;


        float dist = sqrt(dx*dx + dy*dy);
        float d = max(characters[i]->w*characters[i]->scaleX, characters[i]->h*characters[i]->scaleY);

        if(dist <= d) {
            characters[i]->takeDamage(1, 1, 1, dx/dist, dy/dist);
        }
    }*/

    float rr = explosionRadius * explosionRadius;

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius; i<explosionRadius; i++) {
        for(int j=-explosionRadius; j<explosionRadius; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    //if(map.tiles[tx + ty*map.w].flammable) {
                    //    map.tiles[tx + ty*map.w].burning = true;
                    //}
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, 20);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        //int r = randi(100, 255);
        //int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        //g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(255, 255, 255, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 0.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }

}














void DoomsDay::DoomsDayProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, /*ExplosionProjectile::Direction dir,*/ float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer);

    map.startEarthquake(Map::EarthquakeType::earthquakeDoomsDay);

    for(int i=0; i<map.characters.size(); i++) {
        float dx = map.characters[i]->x - x;
        float dy = map.characters[i]->y - y;

        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            map.characters[i]->takeDamage(100.0/2.0, 1, 1, 0, 0);
        }
    }
    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->checkCirclePixelCollision(x, y, explosionRadius)) {
            vehicles[i]->takeDamage(50.0);
        }
    }

    float rr = explosionRadius * explosionRadius;

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius; i<explosionRadius; i++) {
        for(int j=-explosionRadius; j<explosionRadius; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
        }
    }

    int numExplodedTiles = min(explodedTiles.size(), 800);
    for(int i=0; i<numExplodedTiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        //if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
        //    explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //}
        explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Oil;
        explosionProjectile->flyingTile.tile.health = randf(1, 4);
        explosionProjectile->flyingTile.tile.flammable = true;
        explosionProjectile->flyingTile.tile.burning = true;
        explosionProjectile->flyingTile.tile.color = map.getOilTileColor();

        int cr = randi(100, 255);
        int cg = (int)mapf(r, 0, explosionRadius, 0, randi(0, 255));
        cg = min(cg, cr);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(cr, cg, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);

        explosionProjectile->duration = randf(0.5, 2);

        explosionProjectile->isNuclear = true;
        //explosionProjectile->createSmokeTrail = randf(0, 1) < 0.01;
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }

    /*for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }*/
//numExplosionProjectiles = 0;
    int n = max(numExplosionProjectiles - explodedTiles.size(), 100);
    /*for(int i=0; i<n; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 1.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }*/

    createExplosion2(x, y, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);
}





























void Napalm::NapalmProjectile::launch(float x, float y, float vx, float vy, vector<Character*> &characters, /*ExplosionProjectile::Direction dir,*/ float explosionRadius, int numProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer);
    /*for(int i=0; i<characters.size(); i++) {
        float dx = characters[i]->x + characters[i]->w*0.5*characters[i]->scaleX - x;
        float dy = characters[i]->y + characters[i]->h*0.5*characters[i]->scaleY - y;


        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            characters[i]->takeDamage(25, 1, 1, dx/dist, dy/dist);
        }
    }*/

/*    float rr = explosionRadius * explosionRadius;

    vector<Map::Tile> explodedTiles;


    for(int i=-explosionRadius; i<explosionRadius; i++) {
        for(int j=-explosionRadius; j<explosionRadius; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
        }
    }*/

    for(int i=0; i<numProjectiles; i++) {
        NapalmFireProjectile *napalmFireProjectile = new NapalmFireProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        napalmFireProjectile->vx = v * cos(angle) + vx;
        napalmFireProjectile->vy = v * sin(angle) + vy;
        float r = randf(0, explosionRadius);
        napalmFireProjectile->x = x + r * cos(angle);
        napalmFireProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        napalmFireProjectile->gravityHasAnEffect = true;
        //napalmFireProjectile->type = ExplosionProjectile::Type::FlyingTile;
        napalmFireProjectile->flyingTile.tile.type = Map::Tile::Type::Oil;
        napalmFireProjectile->flyingTile.tile.color = sf::Color(Map::oilR, Map::oilG, Map::oilB, 150);
        napalmFireProjectile->flyingTile.tile.flammable = true;
        napalmFireProjectile->flyingTile.tile.burning = true;
        napalmFireProjectile->flyingTile.tile.health = randf(1, 6);
        napalmFireProjectile->flyingTile.tile.flameDealDamagePerSecond = 1;


        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //napalmFireProjectile->characters = characters;
        //napalmFireProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(napalmFireProjectile);
    }

    /*for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 0.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        projectiles.push_back(explosionProjectile);
    }*/

}











void Repeller::update(Map &map, float dt) {

    //if(itemUserCharacter->mana <= 0) {
    if(itemMana <= 0) {
        active = false;
    }
    if(active) {
        int numSpawnedProjectiles = 10;
        float radius = 0;

        float x = 0;
        float y = 0;
        float vx = 0;
        float vy = 0;

        if(itemUserCharacter) {
            radius = max(itemUserCharacter->w*itemUserCharacter->scaleX, itemUserCharacter->h*itemUserCharacter->scaleY) * 1.5;
            x = itemUserCharacter->x;
            y = itemUserCharacter->y;
            vx = itemUserCharacter->vx;
            vy = itemUserCharacter->vy;
        }
        else if(itemUserVehicle) {
            radius = max(itemUserVehicle->w*itemUserVehicle->scaleX, itemUserVehicle->h*itemUserVehicle->scaleY) * 1.5;
            x = itemUserVehicle->x;
            y = itemUserVehicle->y;
            vx = itemUserVehicle->vx;
            vy = itemUserVehicle->vy;
        }

        for(int i=0; i<numSpawnedProjectiles; i++) {
            RepellerProjectile *repellerProjectile = new RepellerProjectile();
            float angle = randf(0, 2.0*Pi);
            float v = randf(10, 100);
            repellerProjectile->vx = vx + v * cos(angle);
            repellerProjectile->vy = vy + v * sin(angle);
            float angle2 = randf(0, 2.0*Pi);
            repellerProjectile->x = x + radius * cos(angle2);
            repellerProjectile->y = y + radius * sin(angle2);
            repellerProjectile->gravityHasAnEffect = false;
            int c = randi(100, 255);
            repellerProjectile->color = sf::Color(c, c, c, 150);
            repellerProjectile->color2 = sf::Color(0, 0, 0, 0);
            repellerProjectile->duration = randf(0.5, 1);
            //repellerProjectile->characters = characters;
            repellerProjectile->projectileUserCharacter = itemUserCharacter;
            repellerProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(repellerProjectile);
        }

        for(int i=0; i<projectiles.size(); i++) {
            if(!projectiles[i]->repellerHasAnEffect || dynamic_cast<RepellerProjectile*>(projectiles[i])) {
                continue;
            }
            float dx = projectiles[i]->x - x;
            float dy = projectiles[i]->y - y;
            float d = sqrt(dx*dx + dy*dy);
            float a = 10000;
            if(d != 0) {
                projectiles[i]->vx += a * dx / (d*d);
                projectiles[i]->vy += a * dy / (d*d);
            }
        }
    }
}







void FlameThrower::update(Map &map, float dt) {

    //if(itemUserCharacter->mana <= 0) {
    if(itemMana <= 0) {
        active = false;
    }
    if(active) {
        for(int i=0; i<90; i++) {
            FireProjectile *fireProjectile = new FireProjectile();
            float an = randf(0, 2.0*Pi);
            float d = randf(0, 10);
            float da = randf(-Pi*0.05, Pi*0.05);
            float initialVelocity = randf(initialVelocityMin, initialVelocityMax);

            if(itemUserCharacter) {
                fireProjectile->x = itemUserCharacter->x + d * cos(an);
                fireProjectile->y = itemUserCharacter->y + d * sin(an);

                fireProjectile->vx = itemUserCharacter->vx + initialVelocity * cos(itemUserCharacter->globalAngle + da);
                fireProjectile->vy = itemUserCharacter->vy + initialVelocity * sin(itemUserCharacter->globalAngle + da);
            }
            else if(itemUserVehicle) {
                fireProjectile->x = itemUserVehicle->x + d * cos(an);
                fireProjectile->y = itemUserVehicle->y + d * sin(an);

                fireProjectile->vx = itemUserVehicle->vx + initialVelocity * cos(itemUserVehicle->globalAngle + da);
                fireProjectile->vy = itemUserVehicle->vy + initialVelocity * sin(itemUserVehicle->globalAngle + da);
            }



            fireProjectile->gravityHasAnEffect = false;
            //fireProjectile->initialVelocityMin = initialVelocityMin;
            //fireProjectile->initialVelocityMax = initialVelocityMax;
            fireProjectile->risingVelocityMin = risingVelocityMin;
            fireProjectile->risingVelocityMax = risingVelocityMax;
            //fireProjectile->airResistance = randf(airResistanceMin, airResistanceMax);
            fireProjectile->airResistanceMin = airResistanceMin;
            fireProjectile->airResistanceMax = airResistanceMax;
            /*int g = randi(0, 255);
            int r = randi(g, 255);
            int a = randi(50, 255);
            fireProjectile->color = sf::Color(255, 0, 0, 0);
            fireProjectile->color2 = sf::Color(r, g, 0, a);*/

            int g = randi(180, 255);
            int r = randi(g, 255);
            int a = randi(50, 255);
            fireProjectile->color = sf::Color(r, g, 0, a);
            fireProjectile->color2 = sf::Color(100, 0, 0, 255);


            fireProjectile->duration = randf2(0.1, 1);

            fireProjectile->duration2 = randf2(0.1, 0.3);
            fireProjectile->color3 = sf::Color(0, 0, 0, 0);


            //fireProjectile->characters = characters;
            fireProjectile->projectileUserCharacter = itemUserCharacter;
            fireProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(fireProjectile);
        }
    }
}
















void LaserSight::update(Map &map, float dt) {

    //if(itemUserCharacter->mana <= 0) {
    if(itemMana <= 0) {
        active = false;
    }
    if(active) {

        float angle = 0;
        float x = 0;
        float y = 0;

        if(itemUserCharacter) {
            angle = itemUserCharacter->globalAngle;
            x = itemUserCharacter->x;
            y = itemUserCharacter->y;
        }
        else if(itemUserVehicle) {
            angle = itemUserVehicle->globalAngle;
            x = itemUserVehicle->x;
            y = itemUserVehicle->y;
        }

        int maxLength = 1000*scaleX;

        float mw = map.w * map.scaleX;
        float mh = map.h * map.scaleY;
        float mx = screenW/2 - mw/2;
        float my = screenH/2 - mh/2;

        float dx = cos(angle);
        float dy = sin(angle);

        for(int i=0; i<maxLength; i++) {

            float d = randf(1,10);
            x += dx * d;
            y += dy * d;
            LaserSightProjectile *laserSightProjectile = new LaserSightProjectile();
            //float angle = randf(0, 2.0*Pi);
            //float v = randf(10, 100);
            //repellerProjectile->vx = itemUserCharacter->vx + v * cos(angle);
            //repellerProjectile->vy = itemUserCharacter->vy + v * sin(angle);
            //float angle2 = randf(0, 2.0*Pi);
            //repellerProjectile->x = x + radius * cos(angle2);
            //repellerProjectile->y = y + radius * sin(angle2);
            laserSightProjectile->x = x;
            laserSightProjectile->y = y;
            laserSightProjectile->gravityHasAnEffect = false;
            int c = randi(0, 150);
            laserSightProjectile->color = sf::Color(255, 0, 0, c);
            laserSightProjectile->color2 = sf::Color(255, 0, 0, c);
            laserSightProjectile->duration = 0;
            //laserSightProjectile->characters = characters;
            laserSightProjectile->projectileUserCharacter = itemUserCharacter;
            laserSightProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(laserSightProjectile);

            if(x >= mx + mw) {
                break;
            }
            else if(x < mx) {
                break;
            }
            else if(y > my + mh) {
                break;
            }
            else if(y < my) {
                break;
            }
            else {
                int px = map.mapX(x, screenW);
                int py = map.mapY(y, screenH);
                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type != Map::Tile::None) {
                        break;
                    }
                }
            }
        }


        /*for(int i=0; i<numSpawnedProjectiles; i++) {
            RepellerProjectile *repellerProjectile = new RepellerProjectile();
            float angle = randf(0, 2.0*Pi);
            float v = randf(10, 100);
            repellerProjectile->vx = itemUserCharacter->vx + v * cos(angle);
            repellerProjectile->vy = itemUserCharacter->vy + v * sin(angle);
            float angle2 = randf(0, 2.0*Pi);
            repellerProjectile->x = x + radius * cos(angle2);
            repellerProjectile->y = y + radius * sin(angle2);
            repellerProjectile->gravityHasAnEffect = false;
            int c = randi(100, 255);
            repellerProjectile->color = sf::Color(c, c, c, 150);
            repellerProjectile->color2 = sf::Color(0, 0, 0, 0);
            repellerProjectile->duration = randf(0.5, 1);
            repellerProjectile->characters = characters;
            repellerProjectile->projectileUserCharacter = itemUserCharacter;
            projectiles.push_back(repellerProjectile);
        }*/

    }
}



void ReflectorBeam::update(Map &map, float dt) {

    //if(itemUserCharacter->mana <= 0) {
    if(itemMana <= 0) {
        active = false;
    }
    if(active) {

        float angle = 0;
        float x = 0;
        float y = 0;

        if(itemUserCharacter) {
            angle = itemUserCharacter->globalAngle;
            x = itemUserCharacter->x;
            y = itemUserCharacter->y;
        }
        else if(itemUserVehicle) {
            angle = itemUserVehicle->globalAngle;
            x = itemUserVehicle->x;
            y = itemUserVehicle->y;
        }

        int maxLength = 2000;
        int maxReflections = 10;
        int numReflections = 0;
        //int tileCheckingRadius = 4;

        float mw = map.w * map.scaleX;
        float mh = map.h * map.scaleY;
        float mx = screenW/2 - mw/2;
        float my = screenH/2 - mh/2;

        float dx = cos(angle);
        float dy = sin(angle);
        float d = 1.5;

        for(int i=0; i<maxLength; i++) {

            if(i % 2 == 0) {
                bool characterHit = false;
                for(int i=0; i<map.characters.size(); i++) {
                    if(map.characters[i] == itemUserCharacter) continue;

                    /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX*0.5;
                    float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY*0.5;
                    float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX*0.5;
                    float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY*0.5;

                    if(isWithinRect(x, y, ax, ay, bx, by)) {
                        map.characters[i]->takeDamage(40*dt, 1.5, 0.15, 0, 0);
                        characterHit = true;
                    }*/
                    if(map.characters[i]->intersects(x, y)) {
                        map.characters[i]->takeDamage(40*dt, 1.5, 0.15, 0, 0);
                        characterHit = true;
                    }
                }
                if(characterHit) {
                    break;
                }

                bool vehicleHit = false;
                for(int i=0; i<vehicles.size(); i++) {
                    if(vehicles[i] == itemUserVehicle) continue;
                    if(vehicles[i]->checkPixelPixelCollision(x, y)) {
                        vehicles[i]->takeDamage(40*dt);
                        vehicleHit = true;
                    }
                }
                if(vehicleHit) {
                    break;
                }
            }

            //float d = 1;
            //float dx = cos(angle) * d;
            //float dy = sin(angle) * d;
            x += dx * d;
            y += dy * d;

            if(randf(0, 1) < 0.02) {
                ReflectorBeamProjectile *reflectorBeamProjectile = new ReflectorBeamProjectile();
                float a = randf(0, 2.0*Pi);
                float v = randf(0, 40);
                reflectorBeamProjectile->vx = v * cos(a);
                reflectorBeamProjectile->vy = v * sin(a);
                reflectorBeamProjectile->x = x;
                reflectorBeamProjectile->y = y;
                reflectorBeamProjectile->gravityHasAnEffect = false;
                reflectorBeamProjectile->repellerHasAnEffect = false;
                int c = randi(100, 255);
                reflectorBeamProjectile->color = sf::Color(255, 0, 0, c);
                reflectorBeamProjectile->color2 = sf::Color(255, 0, 0, 0);
                //reflectorBeamProjectile->color2 = sf::Color(255, 0, 0, c);
                reflectorBeamProjectile->duration = randf2(0.2, 1);
                //reflectorBeamProjectile->characters = characters;
                reflectorBeamProjectile->projectileUserCharacter = itemUserCharacter;
                reflectorBeamProjectile->projectileUserVehicle = itemUserVehicle;
                projectiles.push_back(reflectorBeamProjectile);
            }
            else {
                ReflectorBeamProjectile *reflectorBeamProjectile = new ReflectorBeamProjectile();
                reflectorBeamProjectile->vx = 0;
                reflectorBeamProjectile->vy = 0;
                reflectorBeamProjectile->x = x;
                reflectorBeamProjectile->y = y;
                reflectorBeamProjectile->gravityHasAnEffect = false;
                reflectorBeamProjectile->repellerHasAnEffect = false;
                int c = randi(0, 150);
                reflectorBeamProjectile->color = sf::Color(255, 0, 0, c);
                reflectorBeamProjectile->color2 = sf::Color(255, 0, 0, c);
                reflectorBeamProjectile->duration = 0.01;
                //reflectorBeamProjectile->characters = characters;
                reflectorBeamProjectile->projectileUserCharacter = itemUserCharacter;
                reflectorBeamProjectile->projectileUserVehicle = itemUserVehicle;
                projectiles.push_back(reflectorBeamProjectile);
            }
            float rx = 0, ry = 0;
            bool collided = map.getCollisionReflection(x, y, dx, dy, rx, ry);

            if(collided) {
                //angle = atan2(ry, rx);
                angle = rx != 0 ? atan2(ry, rx) : 0; // TODO check this

                dx = cos(angle);
                dy = sin(angle);

                x += dx * 3;
                y += dy * 3;

                /*for(int j=0; j<1; j++) {  TODO check this!
                    ReflectorBeamProjectile *reflectorBeamProjectile = new ReflectorBeamProjectile();
                    float a = randf(0, 2.0*Pi);
                    float v = randf(10, 40);
                    reflectorBeamProjectile->vx = v * cos(a);
                    reflectorBeamProjectile->vy = v * sin(a);
                    reflectorBeamProjectile->x = x;
                    reflectorBeamProjectile->y = y;
                    reflectorBeamProjectile->gravityHasAnEffect = false;
                    reflectorBeamProjectile->repellerHasAnEffect = false;
                    int c = randi(100, 255);
                    reflectorBeamProjectile->color = sf::Color(c, c, c, 255);
                    reflectorBeamProjectile->color2 = sf::Color(c, c, c, 0);

                    //reflectorBeamProjectile->color = map.tiles[px + py*map.w].color;
                    //reflectorBeamProjectile->color2 = map.tiles[px + py*map.w].color;
                    reflectorBeamProjectile->duration = randf(0.5, 1);
                    reflectorBeamProjectile->characters = characters;
                    reflectorBeamProjectile->projectileUserCharacter = itemUserCharacter;
                    projectiles.push_back(reflectorBeamProjectile);
                }*/

                numReflections++;
                if(numReflections == maxReflections) {
                    break;
                }
            }
        }
    }
}











void Bolter::use(float x, float y, float vx, float vy, float angle) {

    float m = 0;
    if(itemUserCharacter) {
        m = max(itemUserCharacter->w*itemUserCharacter->scaleX, itemUserCharacter->h*itemUserCharacter->scaleY) * 0.5;
    }
    else if(itemUserVehicle) {
        m = max(itemUserVehicle->w*itemUserVehicle->scaleX, itemUserVehicle->h*itemUserVehicle->scaleY) * 0.5;
    }
    for(int i=0; i<numBolts; i++) {
        BoltProjectile *boltProjectile = new BoltProjectile();
        boltProjectile->x = x + m * cos(angle);
        boltProjectile->y = y + m * sin(angle);
        if(i == 0) {
            boltProjectile->angle = angle;
        }
        else {
            boltProjectile->angle = angle + randf2(-0.02*Pi, 0.02*Pi);
        }
        boltProjectile->vx = boltInitialVelocity * cos(boltProjectile->angle);
        boltProjectile->vy = boltInitialVelocity * sin(boltProjectile->angle);
        if(i == 0) {
            boltProjectile->boltAcceleration = boltAcceleration;
        }
        else {
            boltProjectile->boltAcceleration = boltAcceleration * randf2(0.8, 1.2);
        }
        boltProjectile->gravityHasAnEffect = false;
        boltProjectile->explosionRadius = explosionRadius;
        //boltProjectile->characters = characters;
        boltProjectile->projectileUserCharacter = itemUserCharacter;
        boltProjectile->projectileUserVehicle = itemUserVehicle;

        boltProjectile->flameSoundInstance = soundWrapper.playSoundBuffer(boltProjectile->flameSoundBuffer, true, 4);
        projectiles.push_back(boltProjectile);
    }

    soundWrapper.playSoundBuffer(bolterLaunchSoundBuffer);
}




bool Bolter::BoltProjectile::_update(Map& map, float dt, bool exploded) {


    if(exploded) return exploded;


    //checkInitialSelfCollision();

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter) continue;

        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX*0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY*0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX*0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY*0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            map.characters[i]->takeDamage(100.0/7.0, 1, 1, 0, 0);
            createExplosion(map, x, y, characters, explosionRadius, 5 * explosionRadius, 0, 200);
            exploded = true;
            canBeRemoved = true;
        }*/
        if(map.characters[i]->intersects(x, y)) {
            map.characters[i]->takeDamage(100.0/7.0, 1, 1, 0, 0);
            createExplosion(map, x, y, characters, explosionRadius, 5 * explosionRadius, 0, 100);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle) continue;

        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            vehicles[i]->takeDamage(100.0/7.0);
            createExplosion(map, x, y, characters, explosionRadius, 5 * explosionRadius, 0, 100);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    bool collided = map.checkCollision(x, y);
    if(collided) {
        createExplosion(map, x, y, characters, explosionRadius, 5 * explosionRadius, 0, 100);
        exploded = true;
        canBeRemoved = true;
    }

    return exploded;
}



void Bolter::BoltProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, /*ExplosionProjectile::Direction dir,*/ float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer, false, 10);

    flameSoundInstance->removeWithFadeOut(1.0);
    flameSoundInstance = nullptr; // TODO check this!

    /*for(int i=0; i<characters.size(); i++) {
        float dx = characters[i]->x + characters[i]->w*0.5*characters[i]->scaleX - x;
        float dy = characters[i]->y + characters[i]->h*0.5*characters[i]->scaleY - y;


        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            characters[i]->takeDamage(10, 1, 1, dx/dist, dy/dist);
        }
    }*/

    float rr = explosionRadius * explosionRadius;
    float rrPlus1 = (explosionRadius+1) * (explosionRadius+1);

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius-1; i<explosionRadius+1; i++) {
        for(int j=-explosionRadius-1; j<explosionRadius+1; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
            else if(trr < rrPlus1) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                }
            }
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    /*for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 1.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }*/


    createExplosion2(x, y, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);
}




































void Character::createBloodBurst(Map& map, float x, float y, float dirX, float dirY, /*vector<Character*> &characters,*/ float bloodProjectileRadius, int numBloodProjectiles, float bloodProjectileVelocityMin, float bloodProjectileVelocityMax) {

    //printf("at Character::createBloodBurst() %d\n", numBloodProjectiles);
    //for(int i=0; i<explodedTiles.size(); i++) {
    for(int i=0; i<numBloodProjectiles; i++) {
        BloodProjectile *bloodProjectile = new BloodProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(bloodProjectileVelocityMin, bloodProjectileVelocityMax);
        bloodProjectile->vx = v * cos(angle);
        bloodProjectile->vy = v * sin(angle);

        float r = randf(0, bloodProjectileRadius);
        bloodProjectile->x = x + r * cos(angle);
        bloodProjectile->y = y + r * sin(angle);

        bloodProjectile->gravityHasAnEffect = true;
        bloodProjectile->type = BloodProjectile::Type::FlyingTile;
        bloodProjectile->flyingTile.tile.color = sf::Color(180, 0, 0, 150);
        bloodProjectile->flyingTile.tile.type = Map::Tile::Type::Water;

        //bloodProjectile->characters = characters;
        //bloodProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(bloodProjectile);
    }



}
















void LandGrower::update(Map &map, float dt) {

    /*if(itemUserCharacter->mana <= 0) {
        active = false;
    }*/
    //printf("LandGrower::update() 1\n");
    if(active) {
        //itemUserCharacter->mana = 0;

        float radius = 0;

        if(itemUserCharacter) {
            radius = max(itemUserCharacter->w*itemUserCharacter->scaleX, itemUserCharacter->h*itemUserCharacter->scaleY) * 1.5;
        }
        else if(itemUserVehicle) {
            radius = max(itemUserVehicle->w*itemUserVehicle->scaleX, itemUserVehicle->h*itemUserVehicle->scaleY) * 1.5;
        }
        int numSpawnedProjectiles = 10;
        

        float x = 0;
        float y = 0;
        float vx = 0;
        float vy = 0;

        if(itemUserCharacter) {
            x = itemUserCharacter->x;
            y = itemUserCharacter->y;
            vx = itemUserCharacter->vx;
            vy = itemUserCharacter->vy;
        }
        else if(itemUserVehicle) {
            x = itemUserVehicle->x;
            y = itemUserVehicle->y;
            vx = itemUserVehicle->vx;
            vy = itemUserVehicle->vy;
        }

        for(int i=0; i<numSpawnedProjectiles; i++) {
            LandGrowerProjectile *landGrowerProjectile = new LandGrowerProjectile();
            float angle = randf(-0.5*Pi-0.25, -0.5*Pi+0.25);
            float v = randf(10, 100);
            landGrowerProjectile->vx = vx + v * cos(angle);
            landGrowerProjectile->vy = vy + v * sin(angle);
            float angle2 = randf(0, 2.0*Pi);
            float rf = randf(0.5, 1);
            landGrowerProjectile->x = x + radius * rf * cos(angle2);
            landGrowerProjectile->y = y + radius * rf * sin(angle2);
            landGrowerProjectile->gravityHasAnEffect = false;
            int r = randi(190, 210);
            int g = randi(150, 170);
            int b = randi(110, 130);
            //landGrowerProjectile->color = sf::Color(c, c, c, 150);
            landGrowerProjectile->color = sf::Color(r, g, b, 150);
            landGrowerProjectile->color2 = sf::Color(0, 0, 0, 0);
            landGrowerProjectile->duration = randf(0.5, 1);
            landGrowerProjectile->characters = characters;
            landGrowerProjectile->projectileUserCharacter = itemUserCharacter;
            landGrowerProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(landGrowerProjectile);
        }

        spawnCounter += dt*15.0;

        //map.asd(tilePainter, spawnCounter);
        if(rock && spawnCounter > 110) {
            spawnCounter = 50;
            active = false;
        }
        else if(spawnCounter > 110) {
            spawnCounter = 50;
            active = false;
        }
        else {
            if(spawnTiles) {
                asdLandGrowerTiles(map, spawnCounter);
            }
            else {
                asdLandGrower(map, spawnCounter);
            }
        }
    }
    else {
        spawnCounter = 50;
        //printf("LandGrower::update() 4\n");
    }
}















void Digger::update(Map &map, float dt) {


    //if(itemUserCharacter->mana <= 0) {
    if(itemMana <= 0) {
        //active = false; // TODO fixme
    }
    if(!active) {
        if(itemUserCharacter) {
            itemUserCharacter->speedFactor = 1.0;
        }
        if(itemUserVehicle) {
            itemUserVehicle->speedFactor = 1.0;
        }
    }
    if(active) {
        int diggingRadius = 0;

        if(itemUserCharacter) {
            itemUserCharacter->speedFactor = min(1.0/3.0, 1.0);
            diggingRadius = max(itemUserCharacter->w*scaleX, itemUserCharacter->h*scaleY)*0.5;

        }
        if(itemUserVehicle) {
            itemUserVehicle->speedFactor = min(2.0/3.0, 1.0);
            diggingRadius = max(itemUserVehicle->w*scaleX, itemUserVehicle->h*scaleY)*0.5;
        }


        float diggingVelocityMin = 100;
        float diggingVelocityMax = 1000;
        float diggingDistance = 10;

        float rr = diggingRadius * diggingRadius;

        vector<Map::Tile> diggedTiles;

        float x = 0;
        float y = 0;

        if(itemUserCharacter) {
            x = itemUserCharacter->x + diggingDistance * cos(itemUserCharacter->globalAngle);
            y = itemUserCharacter->y + diggingDistance * sin(itemUserCharacter->globalAngle);
        }
        if(itemUserVehicle) {
            x = itemUserVehicle->x + diggingDistance * cos(itemUserVehicle->globalAngle);
            y = itemUserVehicle->y + diggingDistance * sin(itemUserVehicle->globalAngle);
        }

        for(int i=-diggingRadius; i<diggingRadius; i++) {
            for(int j=-diggingRadius; j<diggingRadius; j++) {
                float trr = i*i + j*j;
                if(trr < rr) {
                    float tx = map.mapX(x+i, screenW);
                    float ty = map.mapY(y+j, screenH);
                    //if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.isTileWithin(tx, ty) && (map.tiles[tx + ty*map.w].type == Map::Tile::Dirt || map.tiles[tx + ty*map.w].type == Map::Tile::Ground)) {
                        diggedTiles.push_back(map.tiles[tx + ty*map.w]);
                        map.tiles[tx + ty*map.w] = map.emptyTile;
                    }
                }
            }
        }


        for(int i=0; i<diggedTiles.size(); i++) {
            ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
            float angle = 0;
            
            
            if(itemUserCharacter) {
                angle = itemUserCharacter->globalAngle - Pi + randf(-0.5*Pi, 0.5*Pi);
            }
            if(itemUserVehicle) {
                angle = itemUserVehicle->globalAngle - Pi + randf(-0.5*Pi, 0.5*Pi);
            }


            float v = randf(diggingVelocityMin, diggingVelocityMax);
            explosionProjectile->vx = v * cos(angle);
            explosionProjectile->vy = v * sin(angle);
            float r = randf(0, diggingRadius);
            float angle2 = randf(0, 2.0*Pi);
            explosionProjectile->x = x + r * cos(angle);
            explosionProjectile->y = y + r * sin(angle);

            explosionProjectile->gravityHasAnEffect = true;

            //TODO uncomment this after fixing tile-character collision???
            //explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
            explosionProjectile->type = ExplosionProjectile::Type::BasicWithCollision;
            explosionProjectile->color = diggedTiles[i].color;
            explosionProjectile->color2 = diggedTiles[i].color;
            explosionProjectile->color2.a = 0;

            explosionProjectile->flyingTile.tile = diggedTiles[i];

            if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
                explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
            }
            explosionProjectile->characters = characters;
            explosionProjectile->projectileUserCharacter = itemUserCharacter;
            explosionProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(explosionProjectile);
        }

        int numSpawnedProjectiles = 5;
        float radius = 0;
        float x2 = 0;
        float y2 = 0;

        if(itemUserCharacter) {
            radius = max(itemUserCharacter->w*itemUserCharacter->scaleX, itemUserCharacter->h*itemUserCharacter->scaleY) * 0.5;
            x2 = itemUserCharacter->x;
            y2 = itemUserCharacter->y;
        }
        else if(itemUserVehicle) {
            radius = max(itemUserVehicle->w*itemUserVehicle->scaleX, itemUserVehicle->h*itemUserVehicle->scaleY) * 0.5;
            x2 = itemUserVehicle->x;
            y2 = itemUserVehicle->y;

        }

        for(int i=0; i<numSpawnedProjectiles; i++) {
            DiggerProjectile *diggerProjectile = new DiggerProjectile();
            float angle = randf(0, 2.0*Pi);
            float v = randf(30, 100);

            if(itemUserCharacter) {
                diggerProjectile->vx = itemUserCharacter->vx*itemUserCharacter->speedFactor + v * cos(itemUserCharacter->globalAngle - Pi);
                diggerProjectile->vy =  itemUserCharacter->vy*itemUserCharacter->speedFactor + v * sin(itemUserCharacter->globalAngle - Pi);
            }
            else if(itemUserVehicle) {
                diggerProjectile->vx = itemUserVehicle->vx*itemUserVehicle->speedFactor + v * cos(itemUserVehicle->globalAngle - Pi);
                diggerProjectile->vy =  itemUserVehicle->vy*itemUserVehicle->speedFactor + v * sin(itemUserVehicle->globalAngle - Pi);
            }

            float angle2 = randf(0, 2.0*Pi);
            diggerProjectile->x = x2 + radius * cos(angle2);
            diggerProjectile->y = y2 + radius * sin(angle2);
            diggerProjectile->gravityHasAnEffect = false;
            int c = randi(100, 255);
            diggerProjectile->color = sf::Color(c, c, c, 150);
            diggerProjectile->color2 = sf::Color(0, 0, 0, 0);
            diggerProjectile->duration = randf(0.5, 1);
            diggerProjectile->characters = characters;
            diggerProjectile->projectileUserCharacter = itemUserCharacter;
            diggerProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(diggerProjectile);
        }
    }

}














void JetPack::JetPackProjectile::update(Map &map, float dt) {

    if(gravityHasAnEffect) {
        vy += gravity * dt;
    }
    x += vx * dt;
    y += vy * dt;

    time += dt;

    if(time >= duration) {
        canBeRemoved = true;
        return;
    }

    /*for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter) continue;

        if(map.characters[i]->intersects(x, y)) {
            map.characters[i]->takeDamage(dt*0.1, 1, 0.15, 0, 0);
        }
    }*/

    int px = map.mapX(x, screenW);
    int py = map.mapY(y, screenH);
    if(px != -1 && py != -1) {

        if(map.tiles[px + py*map.w].type != Map::Tile::None) {
            createSmoke(x, y, map.characters, 1, 20, 200);
            /*if(map.tiles[px + py*map.w].flammable && !map.tiles[px + py*map.w].burning) {
                map.tiles[px + py*map.w].burning = true;
            }*/

            canBeRemoved = true;
        }
    }
}








void JetPack::JetPackProjectile2::update(Map &map, float dt) {

    if(gravityHasAnEffect) {
        vy += gravity * dt;
    }

    vy -= dt * risingAcceleration;

    vx = vx * 0.9;
    vy = vy * 0.9;

    x += vx * dt;
    y += vy * dt;

    time += dt;

    if(time >= duration1 + duration2) {
        canBeRemoved = true;
        return;
    }

    /*for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter) continue;

        if(map.characters[i]->intersects(x, y)) {
            map.characters[i]->takeDamage(dt*0.1, 1, 0.15, 0, 0);
        }
    }*/

    if(time < duration1) {
        int px = map.mapX(x, screenW);
        int py = map.mapY(y, screenH);
        if(px != -1 && py != -1) {

            if(map.tiles[px + py*map.w].type != Map::Tile::None) {
                createSmoke(x, y, map.characters, 1, 20, 200);
                /*if(map.tiles[px + py*map.w].flammable && !map.tiles[px + py*map.w].burning) {
                    map.tiles[px + py*map.w].burning = true;
                }*/

                canBeRemoved = true;
            }
        }
    }
}











void JetPack::update(Map &map, float dt) {

    if(itemMana <= 0) {

    }
    if(!active) {

    }
    if(active) {
        if(itemUserCharacter) {
            itemUserCharacter->vy += -1.25*gravity*dt;
        }
        else if(itemUserVehicle) {
            itemUserVehicle->vy += -1.25*gravity*dt;
        }
        
        int numProjectiles = 20;
        float radius = 1*scaleX;

        float x = 0;
        float y = 0;
        

        if(itemUserCharacter) {
            x = itemUserCharacter->x;
            y = itemUserCharacter->y + itemUserCharacter->h*itemUserCharacter->scaleY*0.7; //TODO check this
        }
        else if(itemUserVehicle) {
            x = itemUserVehicle->x;
            y = itemUserVehicle->y + itemUserVehicle->h*itemUserVehicle->scaleY*0.7;
        }


        /*for(int i=0; i<numProjectiles; i++) {
            JetPackProjectile *jetPackProjectile = new JetPackProjectile();
            //float angle = randf(0.5*Pi-0.1, 0.5*Pi+0.1);
            float angle = randf(0, 2.0*Pi);
            float v = randf(0, 100);
            //jetPackProjectile->vx = itemUserCharacter->vx + v * cos(angle);
            //jetPackProjectile->vy = itemUserCharacter->vy + v * sin(angle);
            jetPackProjectile->vx = v * cos(angle);
            jetPackProjectile->vy = v * sin(angle);
            float angle2 = randf(0, 2.0*Pi);
            jetPackProjectile->x = x + radius * cos(angle2);
            jetPackProjectile->y = y + radius * sin(angle2);
            jetPackProjectile->gravityHasAnEffect = false;
            int c = randi(50, 200);
            int a = randi(50, 255);
            jetPackProjectile->color = sf::Color(c, c, c, a);
            jetPackProjectile->color2 = sf::Color(0, 0, 0, 0);
            jetPackProjectile->duration = randf(0.5, 1);
            //jetPackProjectile->characters = characters;
            jetPackProjectile->projectileUserCharacter = itemUserCharacter;
            jetPackProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(jetPackProjectile);
        }*/

        if(itemUserVehicle) {
            for(int i=0; i<numProjectiles; i++) {
                JetPackProjectile *jetPackProjectile = new JetPackProjectile();
                float angle = randf(0.5*Pi-0.05, 0.5*Pi+0.05);
                float v = randf(50, 250);
                jetPackProjectile->vx = v * cos(angle);
                jetPackProjectile->vy = v * sin(angle);
                float angle2 = randf(0, 2.0*Pi);
                jetPackProjectile->x = x + radius * cos(angle2);
                jetPackProjectile->y = y + radius * sin(angle2);
                jetPackProjectile->gravityHasAnEffect = false;
                int g = randi(0, 255);
                int r = randi(g, 255);
                int a = randi(50, 255);
                jetPackProjectile->color = sf::Color(r, g, 0, a);
                jetPackProjectile->color2 = sf::Color(200, 0, 0, 0);
                jetPackProjectile->duration = randf(0.5, 1);
                //jetPackProjectile->characters = characters;
                jetPackProjectile->projectileUserCharacter = itemUserCharacter;
                jetPackProjectile->projectileUserVehicle = itemUserVehicle;
                projectiles.push_back(jetPackProjectile);
            }
        }
        else if(itemUserCharacter) {
            for(int i=0; i<40; i++) {
                JetPackProjectile2 *p = new JetPackProjectile2();
                float d = randf2(0, 7);
                float a1 = randf2(0, 2.0*Pi);
                p->x = x + d * cos(a1);
                p->y = y + d * sin(a1);

                float angle = randf(0.5*Pi-0.05, 0.5*Pi+0.05);
                float v = randf(250, 800);
                p->vx = v * cos(angle);
                p->vy = v * sin(angle);

                /*float v = randf2(10, 200);
                float a2 = randf2(0, 2.0*Pi);
                p->vx = 0.5*itemUserCharacter->vx + v * cos(a2);
                p->vy = 0.5*itemUserCharacter->vy + v * sin(a2);*/

                float alpha1 = randf2(150, 255);
                float g1 = randf2(0, 255);
                float b1 = randf2(0, g1*0.5);
                p->color1 = sf::Color(255, g1, b1, alpha1);

                float c = randf2(50, 150);
                float alpha2 = randf2(100, 150);
                p->color2 = sf::Color(c, c, c, alpha2);

                float c2 = randf2(0, 50);
                float alpha3 = randf2(0, 50);
                p->color3 = sf::Color(c2, c2, c2, alpha3);

                /*float g2 = randf2(50, 150);
                float r2 = randf2(g2, 255);
                float alpha2 = randf2(50, 150);
                p->color2 = sf::Color(r2, g2, 0, alpha2);

                float r3 = randf2(0, 50);
                float alpha3 = randf2(0, 50);
                p->color3 = sf::Color(r3, 0, 0, alpha3);*/

                p->duration1 = randf2(0.2, 0.5);
                p->duration2 = randf2(1.0, 1.5);
                
                p->gravityHasAnEffect = false;
                p->risingAcceleration = randf2(50, 500);

                projectiles.push_back(p);
            }
        }

    }

}












bool NuclearBomb::NuclearBombProjectile::_update(Map& map, float dt, int m, bool exploded) {
    /*if(gravityHasAnEffect) {
        vy += gravity * dt;
    }
    x += vx * dt;
    y += vy * dt;*/
    if(exploded) return exploded;

    float explosionRadius = 350;
    int numExplosionProjectiles = 10000;
    float explosionProjectileVelocityMin = 0;
    float explosionProjectileVelocityMax = 500;

    if(explode) {
        createExplosion(map, x, y, characters, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        this->canBeRemoved = true;
        exploded = true;
        explode = false;
        return true;
    }



    int numWarningProjectiles = 50/(m == 0 ? 1 : m);

    for(int i=0; i<numWarningProjectiles; i++) {
        NuclearWarningProjectile *nuclearWarningProjectile = new NuclearWarningProjectile();

        float angle = randf(0, 2.0*Pi);
        float v = randf(10, 100);
        nuclearWarningProjectile->vx = v * cos(angle);
        nuclearWarningProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        nuclearWarningProjectile->x = x + explosionRadius * cos(angle);
        nuclearWarningProjectile->y = y + explosionRadius * sin(angle);
        nuclearWarningProjectile->gravityHasAnEffect = false;
        int c = randi(30, 255);
        nuclearWarningProjectile->color = sf::Color(255, 0, 0, c);
        nuclearWarningProjectile->color2 = sf::Color(0, 0, 0, 0);
        nuclearWarningProjectile->duration = randf(0.5, 1);
        nuclearWarningProjectile->characters = characters;
        nuclearWarningProjectile->projectileUserCharacter = projectileUserCharacter;
        projectiles.push_back(nuclearWarningProjectile);
    }

    /*time += dt;

    if(time >= duration) {
        canBeRemoved = true;
        return;
    }*/

    float rx = 0, ry = 0;

    /*bool collided = map.getCollisionReflection(x, y, vx, vy, rx, ry);

    if(collided && rx < 1000 && ry < 1000) {
        float d = sqrt(vx*vx + vy*vy);
        vx = rx*d*0.8;
        vy = ry*d*0.8;*/
    float d = sqrt(vx*vx + vy*vy);
    if(d == 0) d = 1;
    bool collided = map.getCollisionReflection(x, y, vx/d, vy/d, rx, ry);

    if(collided && rx < 1000 && ry < 1000) {
        vx = rx*d*0.8;
        vy = ry*d*0.8;

        gravityHasAnEffect = false;

        if(d < 0.1) {
            vx = 0;
            vy = 0;
        }
    }
    else {
        gravityHasAnEffect = true;
    }



    /*float mw = map.w * map.scaleX;
    float mh = map.h * map.scaleY;
    float mx = screenW/2 - mw/2;
    float my = screenH/2 - mh/2;


    if(x >= mx + mw) {
        vx = 0;
        vy = 0;
        gravityHasAnEffect = false;
    }
    else if(x < mx) {
        vx = 0;
        vy = 0;
        gravityHasAnEffect = false;
    }
    else if(y > my + mh) {
        vx = 0;
        vy = 0;
        gravityHasAnEffect = false;
    }
    else if(y < my) {
        vx = 0;
        vy = 0;
        gravityHasAnEffect = false;
    }

    int px = map.mapX(x, screenW);
    int py = map.mapY(y, screenH);
    if(px != -1 && py != -1) {
        bool collided = false;
        if(map.tiles[px + py*map.w].type == Map::Tile::Ground || map.tiles[px + py*map.w].type == Map::Tile::Dirt) {
            vx = 0;
            vy = 0;
            gravityHasAnEffect = false;
        }
    }*/

    return false;
}



























bool BouncyBomb::BouncyBombProjectile::_update(Map& map, float dt, int m, bool exploded) {
    /*if(gravityHasAnEffect) {
        vy += gravity * dt;
    }
    x += vx * dt;
    y += vy * dt;*/
    if(exploded) return exploded;

    /*float explosionRadius = 350;
    int numExplosionProjectiles = 15000;
    float explosionProjectileVelocityMin = 0;
    float explosionProjectileVelocityMax = 500;*/


    checkInitialSelfCollision();

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter && initialSelfCollision) continue;

        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX*0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY*0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX*0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY*0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            createExplosion(map, x, y, characters, 75, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true;
        }*/
        if(map.characters[i]->intersects(x, y)) {
            createExplosion(map, x, y, characters, 75, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true;
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle && initialSelfCollision) {
            continue;
        }
        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            createExplosion(map, x, y, characters, 75, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true;
        }
    }


    /*if(explode) {
        createExplosion(map, x, y, characters, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        this->canBeRemoved = true;
        exploded = true;
        explode = false;
    }*/



    /*int numWarningProjectiles = 50/m;

    for(int i=0; i<numWarningProjectiles; i++) {
        NuclearWarningProjectile *nuclearWarningProjectile = new NuclearWarningProjectile();

        float angle = randf(0, 2.0*Pi);
        float v = randf(10, 100);
        nuclearWarningProjectile->vx = v * cos(angle);
        nuclearWarningProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        nuclearWarningProjectile->x = x + explosionRadius * cos(angle);
        nuclearWarningProjectile->y = y + explosionRadius * sin(angle);
        nuclearWarningProjectile->gravityHasAnEffect = false;
        int c = randi(30, 255);
        nuclearWarningProjectile->color = sf::Color(255, 0, 0, c);
        nuclearWarningProjectile->color2 = sf::Color(0, 0, 0, 0);
        nuclearWarningProjectile->duration = randf(0.5, 1);
        nuclearWarningProjectile->characters = characters;
        nuclearWarningProjectile->projectileUserCharacter = projectileUserCharacter;
        projectiles.push_back(nuclearWarningProjectile);
    }*/

    /*time += dt;

    if(time >= duration) {
        canBeRemoved = true;
        return;
    }*/

    float rx = 0, ry = 0;

    float d = sqrt(vx*vx + vy*vy);
    if(d == 0) d = 1;
    bool collided = map.getCollisionReflection(x, y, vx/d, vy/d, rx, ry);

    if(collided && rx < 1000 && ry < 1000) {    // TODO fix that hack

        //soundWrapper.playSoundBuffer(collisionSoundBuffer, false, 4);
        x += rx;
        y += ry;
        vx = rx*d*0.999;
        vy = ry*d*0.999;

        gravityHasAnEffect = false;
        float d = sqrt(vx*vx + vy*vy);
        if(d < 1) {
            vx = 0;
            vy = 0;
        }
        numBounces++;
        if(numBounces >= maxBounces) {
            createExplosion(map, x, y, characters, 75, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true;
        }
    }
    else {
        gravityHasAnEffect = true;
    }


    //if(numBounces >= 0.9 * maxBounces) {
        createSmoke(x, y, map.characters, 1, 20, 200);
        //createFlame(x, y, map.characters, 1, 20, 200, 0);
    //}




    return false;
}
















void Rifle::RifleProjectile::update(Map& map, float dt) {

    float d = 1;
    float dx = cos(angle) * d;
    float dy = sin(angle) * d;

    for(int i=0; i<10000; i++) {

        x += dx;
        y += dy;

        bool characterHit = false;
        for(int k=0; k<map.characters.size(); k++) {
            if(map.characters[k] == projectileUserCharacter) continue;

            float ax = map.characters[k]->x - map.characters[k]->w*map.characters[k]->scaleX*0.5;
            float ay = map.characters[k]->y - map.characters[k]->h*map.characters[k]->scaleY*0.5;
            float bx = map.characters[k]->x + map.characters[k]->w*map.characters[k]->scaleX*0.5;
            float by = map.characters[k]->y + map.characters[k]->h*map.characters[k]->scaleY*0.5;

            if(isWithinRect(x, y, ax, ay, bx, by)) {
                map.characters[k]->takeDamage(50, 1, 1, 0, 0);
                characterHit = true;
                break;
            }
        }

        bool vehicleHit = false;
        for(int k=0; k<vehicles.size(); k++) {
            if(vehicles[k] == projectileUserVehicle) continue;

            if(vehicles[k]->checkPixelPixelCollision(x, y)) {
                vehicles[k]->takeDamage(20);
                vehicleHit = true;
                break;
            }
        } 

        /*if(characterHit || vehicleHit) {
            canBeRemoved = true;
            break;
        }*/

        bool collided = map.checkCollision(x, y);

        if(collided || characterHit || vehicleHit) {
            for(int i=0; i<200; i++) {
                RifleHitProjectile *rifleHitProjectile = new RifleHitProjectile();
                float a = randf(0, 2.0*Pi);
                float v = randf(0, 250);
                rifleHitProjectile->vx = v * cos(a);
                rifleHitProjectile->vy = v * sin(a);
                rifleHitProjectile->x = x;
                rifleHitProjectile->y = y;
                rifleHitProjectile->gravityHasAnEffect = false;
                rifleHitProjectile->repellerHasAnEffect = true;
                int c = randi(20, 150);
                rifleHitProjectile->color = sf::Color(255, 255, 255, c);
                rifleHitProjectile->color2 = sf::Color(255, 255, 255, 0);
                //reflectorBeamProjectile->color2 = sf::Color(255, 0, 0, c);
                rifleHitProjectile->duration = randf(0.1, 0.5);
                //rifleHitProjectile->characters = characters;
                rifleHitProjectile->projectileUserCharacter = projectileUserCharacter;
                rifleHitProjectile->projectileUserVehicle = projectileUserVehicle;
                projectiles.push_back(rifleHitProjectile);
            }
            canBeRemoved = true;
            break;
        }
    }

}


















bool DoomsDay::DoomsDayProjectile::_update(Map& map, float dt, int m, bool exploded, bool &characterHit, bool &vehicleHit) {

    if(exploded) return exploded;


    counter += dt * 15.0;
    if(counter >= 2.0 * Pi) {
        counter -= 2.0 * Pi;
    }

    int numSmokeProjectiles = 8;

    for(int i=0; i<numSmokeProjectiles; i++) {
        DoomsDaySmokeProjectile *doomsDaySmokeProjectile = new DoomsDaySmokeProjectile();

        //float angle = randf(0, 2.0*Pi);
        float angle = counter + randf(-0.1, 0.1);
        float v = randf(0, 500);
        doomsDaySmokeProjectile->vx = v * cos(angle);
        doomsDaySmokeProjectile->vy = v * sin(angle);
        //float angle2 = randf(0, 2.0*Pi);
        doomsDaySmokeProjectile->x = x;
        doomsDaySmokeProjectile->y = y;
        doomsDaySmokeProjectile->gravityHasAnEffect = false;
        //int c = randi(0, 50);
        int c = 0;
        doomsDaySmokeProjectile->color = sf::Color(c, c, c, randi(50, 255));
        doomsDaySmokeProjectile->color2 = sf::Color(0, 0, 0, 0);
        doomsDaySmokeProjectile->duration = randf(0.5, 2);
        //doomsDaySmokeProjectile->characters = characters;
        doomsDaySmokeProjectile->projectileUserCharacter = projectileUserCharacter;
        doomsDaySmokeProjectile->projectileUserVehicle = projectileUserVehicle;
        projectiles.push_back(doomsDaySmokeProjectile);
    }

    /*struct Testing {
        float tx, ty;
        float currentAngle;
        int i;
        int n;
    };
    vector<Testing> pino;

    if(randf(0, 1) < 0.05) {
        int n = randi(100, 200);
        float angleChangeMin = -0.2*Pi, angleChangeMax = 0.2*Pi;
        float initialAngle = randf(0, 2.0*Pi);
        float currentAngle = initialAngle;
        float tx = x, ty = y;
        int nTotal = n;

        for(int i=0; i<n; i++) {

            if(nTotal < 800 && pino.size() < 15 && randf(0, 1) < 0.01) {
                int nn = randi(50, 100);
                nTotal += nn;
                pino.push_back({tx, ty, currentAngle, i, nn});
            }
            if(i == n-1) {
                if(nTotal < 800 && pino.size() > 0) {
                    //int k = pino.size()-1;
                    int k = 0;
                    tx = pino[k].tx;
                    ty = pino[k].ty;
                    currentAngle = pino[k].currentAngle;
                    i = 0;
                    n = pino[k].n;
                    //pino.pop_back();
                    pino.erase(pino.begin());
                    printf("pino %d\n", pino.size());
                }
            }

            float t = randf(0, 1);
            if(t < 0.02) {
                currentAngle += angleChangeMin;
            }
            else if(t < 0.04) {
                currentAngle += angleChangeMax;
            }
            tx += cos(currentAngle);
            ty += sin(currentAngle);

            DoomsDaySmokeProjectile *doomsDaySmokeProjectile = new DoomsDaySmokeProjectile();

            float a = randf(0, 2.0*Pi);
            float v = randf(0, 10);
            doomsDaySmokeProjectile->vx = v * cos(a);
            doomsDaySmokeProjectile->vy = v * sin(a);

            doomsDaySmokeProjectile->x = tx;
            doomsDaySmokeProjectile->y = ty;
            doomsDaySmokeProjectile->gravityHasAnEffect = false;
            int c = randi(0, 50);
            doomsDaySmokeProjectile->color = sf::Color(c, c, c, randi(50, 255));
            doomsDaySmokeProjectile->color2 = sf::Color(0, 0, 0, 0);
            doomsDaySmokeProjectile->duration = randf(0.25, 1);
            doomsDaySmokeProjectile->characters = characters;
            doomsDaySmokeProjectile->projectileUserCharacter = projectileUserCharacter;
            projectiles.push_back(doomsDaySmokeProjectile);
        }
    }*/





    float rx = 0, ry = 0;
    float d = sqrt(vx*vx + vy*vy);
    if(d == 0) d = 1;

    bool collided = map.getCollisionReflection(x, y, vx/d, vy/d, rx, ry);
    bool collidedCharacter = false;
    bool collidedVehicle = false;

    if(!collided && !characterHit && !vehicleHit) {
        checkInitialSelfCollision();
        for(int i=0; i<map.characters.size(); i++) {
            if(map.characters[i] == projectileUserCharacter && initialSelfCollision) {
                continue;
            }
            else {
                collidedCharacter = map.characters[i]->getCollisionReflection(x, y, vx/d, vy/d, rx, ry);
                if(collidedCharacter) {
                    //x += 5.0*rx;
                    //y += 5.0*ry;
                    characterHit = true;
                    break;
                }
            }
        }

        for(int i=0; i<vehicles.size(); i++) {
            if(vehicles[i] == projectileUserVehicle && initialSelfCollision) {
                continue;
            }
            else {
                collidedVehicle = vehicles[i]->getCollisionReflection(x, y, vx/d, vy/d, rx, ry);
                if(collidedVehicle) {
                    vehicleHit = true;
                    break;
                }
            }
        }
    }

    if((collided || collidedCharacter || collidedVehicle) && rx < 1000 && ry < 1000) {    // TODO fix that hack

        //soundWrapper.playSoundBuffer(collisionSoundBuffer, false, 4);
        createExplosion(map, x, y, characters, 160, 1500, 0, 200);

        float d = sqrt(vx*vx + vy*vy);

        x += rx;
        y += ry;
        vx = rx*d;
        vy = ry*d;

        if(d < 1) {
            vx = 0;
            vy = 0;
        }
        numBounces++;
        if(numBounces >= maxBounces) {
            //createExplosion(map, x, y, characters, 75, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;

            for(int i=0; i<2500; i++) {
                ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
                float angle = randf(0, 2.0*Pi);
                float v = randf(150, 1500);
                explosionProjectile->vx = v * cos(angle);
                explosionProjectile->vy = v * sin(angle);
                float angle2 = randf(0, 2.0*Pi);
                float ra = randf(0, 100);
                explosionProjectile->x = x;// + ra * cos(angle2);
                explosionProjectile->y = y;// + ra * sin(angle2);
                //explosionProjectile->gravityHasAnEffect = false;
                explosionProjectile->type = ExplosionProjectile::Type::Basic;
                //float r = randi(50, 255);
                //float g = min(randi(0, 255), r);
                int r = randi(100, 255);
                int g = (int)mapf(ra, 0, 100, 0, randi(0, 255));
                g = min(g, r);
                int alpha = randi(50, 255);
                explosionProjectile->color = sf::Color(r, g, 0, alpha);
                explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
                explosionProjectile->duration = randf(1, 5);
                explosionProjectile->createSmokeTrail = randf(0, 1) < 0.1;
                //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
                //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
                //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
                //explosionProjectile->characters = characters;
                //explosionProjectile->projectileUserCharacter = itemUserCharacter;
                projectiles.push_back(explosionProjectile);
            }
            smokeSoundInstance->removeWithFadeOut(1.0);
            smokeSoundInstance = nullptr;

            return true;
        }
    }



    return false;
}














void Railgun::RailgunProjectile::update(Map& map, float dt) {

    float d = 1;
    float dx = cos(angle) * d;
    float dy = sin(angle) * d;

    for(int i=0; i<10000; i++) {

        x += dx;
        y += dy;

        bool characterHit = false;
        for(int k=0; k<map.characters.size(); k++) {
            if(map.characters[k] == projectileUserCharacter) continue;

            float ax = map.characters[k]->x - map.characters[k]->w*map.characters[k]->scaleX*0.5;
            float ay = map.characters[k]->y - map.characters[k]->h*map.characters[k]->scaleY*0.5;
            float bx = map.characters[k]->x + map.characters[k]->w*map.characters[k]->scaleX*0.5;
            float by = map.characters[k]->y + map.characters[k]->h*map.characters[k]->scaleY*0.5;

            if(isWithinRect(x, y, ax, ay, bx, by)) {
                //map.characters[k]->takeDamage(100, 1, 1, 0, 0);
                characterHit = true;
                break;
            }
        }

       bool vehicleHit = false;
        for(int k=0; k<vehicles.size(); k++) {
            if(vehicles[k] == projectileUserVehicle) continue;

            if(vehicles[k]->checkPixelPixelCollision(x, y)) {
                vehicleHit = true;
                break;
            }
        }

        for(int i=0; i<1; i++) {
            RailgunSmokeProjectile *railgunSmokeProjectile = new RailgunSmokeProjectile();
            float a = randf(0, 2.0*Pi);
            float v = randf(0, 10);
            railgunSmokeProjectile->vx = v * cos(a);
            railgunSmokeProjectile->vy = v * sin(a);
            railgunSmokeProjectile->x = x;
            railgunSmokeProjectile->y = y;
            railgunSmokeProjectile->gravityHasAnEffect = false;
            railgunSmokeProjectile->repellerHasAnEffect = false;
            int c = randi(40, 200);
            railgunSmokeProjectile->color = sf::Color(c, c, c, 255);
            railgunSmokeProjectile->color2 = sf::Color(c, c, c, 0);
            //reflectorBeamProjectile->color2 = sf::Color(255, 0, 0, c);
            railgunSmokeProjectile->duration = randf(1, 2);
            railgunSmokeProjectile->characters = characters;
            railgunSmokeProjectile->projectileUserCharacter = projectileUserCharacter;
            projectiles.push_back(railgunSmokeProjectile);
        }


        bool collided = map.checkCollision(x, y);

        if(collided || characterHit || vehicleHit) {

            createExplosion(map, x, y, characters, 100, 2000, 0, 200);

            canBeRemoved = true;
            break;
        }
    }

}



void Railgun::RailgunProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer);

    //flameSoundInstance->removeWithFadeOut(1.0);
    //flameSoundInstance = nullptr; // TODO check this!

    for(int i=0; i<map.characters.size(); i++) {
        float dx = map.characters[i]->x - x;
        float dy = map.characters[i]->y - y;

        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            map.characters[i]->takeDamage(100, 1, 1, 0, 0);
        }
    }
    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->checkCirclePixelCollision(x, y, explosionRadius)) {
            vehicles[i]->takeDamage(100);
        }
    }

    float rr = explosionRadius * explosionRadius;
    //float rrPlus1 = (explosionRadius+1) * (explosionRadius+1);

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius-1; i<explosionRadius+1; i++) {
        for(int j=-explosionRadius-1; j<explosionRadius+1; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    /*if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }*/
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
            /*else if(trr < rrPlus1) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                }
            }*/
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        /*int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);*/
        int r = randi(100, 255);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, r, r, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 1.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }

}










void DirtCannon::update(Map &map, float dt) {

        for(int i=0; i<numWarningProjectiles; i++) {
            DirtCannonWarningProjectile *warningProjectile = new DirtCannonWarningProjectile();
            if(itemUserCharacter) {
                float a = itemUserCharacter->globalAngle + randf2(-deltaAngle, deltaAngle);
                float r = randf2(0, range);
                warningProjectile->x = itemUserCharacter->x + r * cos(a);
                warningProjectile->y = itemUserCharacter->y + r * sin(a);
                warningProjectile->vx = itemUserCharacter->vx;
                warningProjectile->vy = itemUserCharacter->vy;
            }
            else if(itemUserVehicle) {
                float a = itemUserVehicle->globalAngle + randf2(-deltaAngle, deltaAngle);
                float r = randf2(0, range);
                warningProjectile->x = itemUserVehicle->x + r * cos(a);
                warningProjectile->y = itemUserVehicle->y + r * sin(a);
                warningProjectile->vx = itemUserVehicle->vx;
                warningProjectile->vy = itemUserVehicle->vy;
            }

            warningProjectile->gravityHasAnEffect = false;
            warningProjectile->repellerHasAnEffect = false;
            warningProjectile->duration = 0.1;
            int c = randi2(50, 200);
            warningProjectile->color = sf::Color(255, 0, 0, c);
            warningProjectile->color2 = sf::Color(255, 0, 0, 0);
            //warningProjectile->characters = characters;
            warningProjectile->projectileUserCharacter = itemUserCharacter;
            warningProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(warningProjectile);
        }

        if(launch) {
            float rr = range * range;

            //vector<Map::Tile> explodedTiles;

            float x = 0;
            float y = 0;

            if(itemUserCharacter) {
                x = itemUserCharacter->x;
                y = itemUserCharacter->y;
            }
            else if(itemUserVehicle) {
                x = itemUserVehicle->x;
                y = itemUserVehicle->y;
            }

            //printf("globalAngle: %f, ", itemUserCharacter->globalAngle);
            //printf("aimAngle: %f, ", itemUserCharacter->aimAngle);

            for(int i=-range; i<range; i++) {
                for(int j=-range; j<range; j++) {
                    float trr = i*i + j*j;
                    if(trr < rr) {
                        float tx = map.mapX(x+i, screenW);
                        float ty = map.mapY(y+j, screenH);
                        if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                            /*if(map.tiles[tx + ty*map.w].flammable) {
                                map.tiles[tx + ty*map.w].burning = true;
                            }*/
                            //explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                            

                            float d = sqrt(trr);
                            float dx = (float)i / d;
                            float dy = (float)j / d;

                            float angle = atan2(dy, dx);
                            bool br = false, bl = false, bl2 = false;

                            if(itemUserCharacter) {
                                br = itemUserCharacter->direction == 
                                    Character::Direction::Right &&
                                (angle < itemUserCharacter->aimAngle + deltaAngle &&
                                    angle > itemUserCharacter->aimAngle - deltaAngle);

                                bl = itemUserCharacter->direction == 
                                    Character::Direction::Left &&
                                (angle < Pi - itemUserCharacter->aimAngle + deltaAngle &&
                                    angle > Pi - itemUserCharacter->aimAngle - deltaAngle);

                                bl2 = itemUserCharacter->direction == 
                                    Character::Direction::Left &&
                                (angle < -Pi - itemUserCharacter->aimAngle + deltaAngle &&
                                    angle > -Pi - itemUserCharacter->aimAngle - deltaAngle);
                            }
                            else if(itemUserVehicle) {
                                br = itemUserVehicle->direction == 
                                    Vehicle::Direction::Right &&
                                (angle < itemUserVehicle->aimAngle + deltaAngle &&
                                    angle > itemUserVehicle->aimAngle - deltaAngle);

                                bl = itemUserVehicle->direction == 
                                    Vehicle::Direction::Left &&
                                (angle < Pi - itemUserVehicle->aimAngle + deltaAngle &&
                                    angle > Pi - itemUserVehicle->aimAngle - deltaAngle);

                                bl2 = itemUserVehicle->direction == 
                                    Vehicle::Direction::Left &&
                                (angle < -Pi - itemUserVehicle->aimAngle + deltaAngle &&
                                    angle > -Pi - itemUserVehicle->aimAngle - deltaAngle);
                            }



                            if(br || bl || bl2) { // TODO check this!

                                ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
                                
                                float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
                                explosionProjectile->vx = v * dx;
                                explosionProjectile->vy = v * dy;
                                //float r = randf(0, explosionRadius);
                                explosionProjectile->x = x + i;
                                explosionProjectile->y = y + j;
                                //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
                                //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
                                explosionProjectile->gravityHasAnEffect = true;
                                explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
                                explosionProjectile->flyingTile.tile = map.tiles[tx + ty*map.w];

                                explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Oil;
                                explosionProjectile->flyingTile.tile.health = randf(1, 4);
                                explosionProjectile->flyingTile.tile.flammable = true;
                                explosionProjectile->flyingTile.tile.burning = true;
                                explosionProjectile->flyingTile.tile.color = map.getOilTileColor();
                                explosionProjectile->createSmokeTrail = randf(0, 1) < 0.1;
                                explosionProjectile->dealDamage = damage;
                                //explosionProjectile->characters = characters;
                                explosionProjectile->projectileUserCharacter = itemUserCharacter;
                                explosionProjectile->projectileUserVehicle = itemUserVehicle;
                                projectiles.push_back(explosionProjectile);

                                map.tiles[tx + ty*map.w] = map.emptyTile;
                            }
                        }
                    }
                }
            }
            launch = false;
        }
    }





    void DirtCannon::use(float x, float y, float vx, float vy, float angle) {


        launch = true;

        soundWrapper.playSoundBuffer(dirtCannonSoundBuffer, false, 100);

        /*float rr = range * range;

        vector<Map::Tile> explodedTiles;

        for(int i=-range; i<range; i++) {
            for(int j=-range; j<range; j++) {
                float trr = i*i + j*j;
                if(trr < rr) {
                    float tx = map.mapX(x+i, screenW);
                    float ty = map.mapY(y+j, screenH);
                    if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                        //if(map.tiles[tx + ty*map.w].flammable) {
                        //    map.tiles[tx + ty*map.w].burning = true;
                        //}
                        //explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                        

                        float d = sqrt(trr);
                        float dx = (float)i / d;
                        float dy = (float)j / d;

                        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
                        float angle = randf(0, 2.0*Pi);
                        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
                        explosionProjectile->vx = v * dx;
                        explosionProjectile->vy = v * dy;
                        //float r = randf(0, explosionRadius);
                        explosionProjectile->x = x + i;
                        explosionProjectile->y = y + j;
                        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
                        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
                        explosionProjectile->gravityHasAnEffect = true;
                        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
                        explosionProjectile->flyingTile.tile = map.tiles[tx + ty*map.w];

                        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
                            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
                        }
                        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
                        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
                        //explosionProjectile->direction = dir;
                        //if(explosionProjectile->vy > 0) {
                        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
                        //}
                        //explosionProjectile->characters = characters;
                        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
                        projectiles.push_back(explosionProjectile);

                        map.tiles[tx + ty*map.w] = map.emptyTile;
                    }
                }
            }
        }*/

        /*for(int i=0; i<explodedTiles.size(); i++) {
            ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
            float angle = randf(0, 2.0*Pi);
            float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
            explosionProjectile->vx = v * cos(angle);
            explosionProjectile->vy = v * sin(angle);
            float r = randf(0, explosionRadius);
            explosionProjectile->x = x + r * cos(angle);
            explosionProjectile->y = y + r * sin(angle);
            //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
            //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
            explosionProjectile->gravityHasAnEffect = true;
            explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
            explosionProjectile->flyingTile.tile = explodedTiles[i];
            if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
                explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
            }
            //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
            //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
            //explosionProjectile->direction = dir;
            //if(explosionProjectile->vy > 0) {
            //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
            //}
            //explosionProjectile->characters = characters;
            //explosionProjectile->projectileUserCharacter = itemUserCharacter;
            projectiles.push_back(explosionProjectile);
        }*/
        //numExplosionProjectiles = 0;
        /*for(int i=0; i<numExplosionProjectiles; i++) {
            ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
            float angle = randf(0, 2.0*Pi);
            float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
            explosionProjectile->vx = v * cos(angle);
            explosionProjectile->vy = v * sin(angle);
            float angle2 = randf(0, 2.0*Pi);
            float ra = randf(0, explosionRadius);
            explosionProjectile->x = x + ra * cos(angle2);
            explosionProjectile->y = y + ra * sin(angle2);
            explosionProjectile->gravityHasAnEffect = false;
            explosionProjectile->type = ExplosionProjectile::Type::Basic;
            //float r = randi(50, 255);
            //float g = min(randi(0, 255), r);
            int r = randi(100, 255);
            int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
            g = min(g, r);
            int alpha = randi(50, 255);
            explosionProjectile->color = sf::Color(r, g, 0, alpha);
            explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
            explosionProjectile->duration = 1.5;
            //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
            //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
            //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
            //explosionProjectile->direction = dir;
            //explosionProjectile->characters = characters;
            //explosionProjectile->projectileUserCharacter = itemUserCharacter;
            projectiles.push_back(explosionProjectile);
        }*/




        
    }










/*void ExplosionProjectile::_update(Map& map, float dt) {


    time += dt;

    if(time >= duration) {
        canBeRemoved = true;
        return;
    }


    if(dealDamage > 0) {
        for(int i=0; i<characters.size(); i++) {
            float ax = characters[i]->x - characters[i]->w*characters[i]->scaleX * 0.5;
            float ay = characters[i]->y - characters[i]->h*characters[i]->scaleY * 0.5;
            float bx = characters[i]->x + characters[i]->w*characters[i]->scaleX * 0.5;
            float by = characters[i]->y + characters[i]->h*characters[i]->scaleY * 0.5;

            if(isWithinRect(x, y, ax, ay, bx, by)) {
                characters[i]->takeDamage(dealDamage, 1, 1, 0, 0);
                canBeRemoved = true;
                return;
            }
        }
    }

    //if(type == Type::FlyingTile) {


        if(x >= map.mx_ + map.mw_) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(map.mx_ + map.mw_ - 1, screenW);
                int py = map.mapY(y, screenH);

                if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                    map.tiles[px + py*map.w] = flyingTile.tile;
                }
            }

            canBeRemoved = true;
        }
        else if(x < map.mx_) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(map.mx_+1, screenW);
                int py = map.mapY(y, screenH);
                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }
            canBeRemoved = true;
        }
        else if(y > map.my_ + map.mh_) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(x, screenW);
                int py = map.mapY(map.my_ + map.mh_ - 1, screenH);

                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }
            canBeRemoved = true;
        }
        else if(y < map.my_) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(x, screenW);
                int py = map.mapY(map.my_+1, screenH);;

                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }
            canBeRemoved = true;
        }
        else if(type == Type::FlyingTile || type == Type::BasicWithCollision) { // check the "if"
            int px = map.mapX(x, screenW);
            int py = map.mapY(y, screenH);
            if(px != -1 && py != -1) {
                if(map.tiles[px + py*map.w].type == Map::Tile::Ground || map.tiles[px + py*map.w].type == Map::Tile::Dirt) {
                    if(type == Type::FlyingTile) {
                        int distUp = 0, distDown = 0, distLeft = 0, distRight = 0;

                        while(map.isTileWithin(px, py-distUp) && (map.tiles[px + (py-distUp)*map.w].type == Map::Tile::Ground || map.tiles[px + (py-distUp)*map.w].type == Map::Tile::Dirt)) {
                            distUp++;
                        }
                        while(map.isTileWithin(px, py+distDown) && (map.tiles[px + (py+distDown)*map.w].type == Map::Tile::Ground || map.tiles[px + (py+distDown)*map.w].type == Map::Tile::Dirt)) {
                            distDown++;
                        }
                        while(map.isTileWithin(px-distLeft, py) && (map.tiles[px-distLeft + py*map.w].type == Map::Tile::Ground || map.tiles[px-distLeft + py*map.w].type == Map::Tile::Dirt)) {
                            distLeft++;
                        }
                        while(map.isTileWithin(px+distRight, py) && (map.tiles[px+distRight + py*map.w].type == Map::Tile::Ground || map.tiles[px+distRight + py*map.w].type == Map::Tile::Dirt)) {
                            distRight++;
                        }

                        if(distLeft <= distUp && distLeft <= distDown && distLeft <= distRight) {
                            direction = Direction::Left;
                        }
                        else if(distRight <= distUp && distLeft <= distDown && distRight <= distLeft) {
                            direction = Direction::Right;
                        }
                        else if(distDown <= distUp) {
                            direction = Direction::Down;
                        }
                        else {
                            direction = Direction::Up;
                        }

                        if(direction == Direction::Up) {
                            if(map.isTileWithin(px, py-distUp)) {
                                map.tiles[px + (py-distUp)*map.w] = flyingTile.tile;
                            }

                        }
                        else if(direction == Direction::Down) {
                            if(map.isTileWithin(px, py+distDown)) {
                                map.tiles[px + (py+distDown)*map.w] = flyingTile.tile;
                            }
                        }
                        if(direction == Direction::Left) {
                            if(map.isTileWithin(px-distLeft, py)) {
                                map.tiles[px-distLeft + py*map.w] = flyingTile.tile;
                            }

                        }
                        else if(direction == Direction::Right) {
                            if(map.isTileWithin(px+distRight, py)) {
                                map.tiles[px+distRight + py*map.w] = flyingTile.tile;
                            }
                        }


                    }
                    canBeRemoved = true;
                }
            }
        }
    //}

}*/









void HeavyFlamer::update(Map &map, float dt) {

    //if(itemUserCharacter->mana <= 0) {
    if(itemMana <= 0) {
        active = false;
    }
    if(active) {
        for(int i=0; i<200; i++) {
            FireProjectile *fireProjectile = new FireProjectile();
            float an = randf(0, 2.0*Pi);
            float d = randf(0, 10);

            fireProjectile->x = d * cos(an);
            fireProjectile->y = d * sin(an);

            float initialVelocity = randf(initialVelocityMin, initialVelocityMax);
            //float da = randf(-Pi*0.05, Pi*0.05);
            float da = randf(-Pi*0.025, Pi*0.025);

            if(itemUserCharacter) {
                fireProjectile->x += itemUserCharacter->x;
                fireProjectile->y += itemUserCharacter->y;

                fireProjectile->vx = itemUserCharacter->vx + initialVelocity * cos(itemUserCharacter->globalAngle + da);
                fireProjectile->vy = itemUserCharacter->vy + initialVelocity * sin(itemUserCharacter->globalAngle + da);
            }
            else if(itemUserVehicle) {
                fireProjectile->x += itemUserVehicle->x;
                fireProjectile->y += itemUserVehicle->y;
                
                fireProjectile->vx = itemUserVehicle->vx + initialVelocity * cos(itemUserVehicle->globalAngle + da);
                fireProjectile->vy = itemUserVehicle->vy + initialVelocity * sin(itemUserVehicle->globalAngle + da);

            }

            fireProjectile->gravityHasAnEffect = false;

            fireProjectile->risingVelocityMin = risingVelocityMin;
            fireProjectile->risingVelocityMax = risingVelocityMax;

            fireProjectile->airResistanceMin = airResistanceMin;
            fireProjectile->airResistanceMax = airResistanceMax;

            fireProjectile->damage = 2.0;

            int g = randi(180, 255);
            int r = randi(g, 255);
            int a = randi(50, 255);
            fireProjectile->color = sf::Color(r, g, 0, a);
            fireProjectile->color2 = sf::Color(100, 0, 0, 255);


            fireProjectile->duration = randf2(0.1, 1);

            fireProjectile->duration2 = randf2(0.1, 0.3);
            fireProjectile->color3 = sf::Color(0, 0, 0, 0);


            //fireProjectile->characters = characters;
            fireProjectile->projectileUserCharacter = itemUserCharacter;
            fireProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(fireProjectile);
        }
    }
}








void LaserCannon::use(float x, float y, float vx, float vy, float angle) {

    LaserBoltProjectile *laserBoltProjectile = new LaserBoltProjectile();
    float m = 0;
    
    if(itemUserCharacter) {
        m = max(itemUserCharacter->w*itemUserCharacter->scaleX, itemUserCharacter->h*itemUserCharacter->scaleY) * 0.5;
    }
    else if(itemUserVehicle) {
        m = max(itemUserVehicle->w*itemUserVehicle->scaleX, itemUserVehicle->h*itemUserVehicle->scaleY) * 0.5;
    }

    laserBoltProjectile->x = x + m * cos(angle);
    laserBoltProjectile->y = y + m * sin(angle);
    laserBoltProjectile->angle = angle;
    laserBoltProjectile->vx = laserBoltVelocity * cos(angle);
    laserBoltProjectile->vy = laserBoltVelocity * sin(angle);
    laserBoltProjectile->gravityHasAnEffect = false;
    //laserBoltProjectile->characters = characters;
    laserBoltProjectile->projectileUserCharacter = itemUserCharacter;
    laserBoltProjectile->projectileUserVehicle = itemUserVehicle;
    laserBoltProjectile->laserBoltExplosionRadius = laserBoltExplosionRadius;
    //laserBoltProjectile->flameSoundInstance = soundWrapper.playSoundBuffer(missileProjectile->flameSoundBuffer, true, 20);
    projectiles.push_back(laserBoltProjectile);


    soundWrapper.playSoundBuffer(laserCannonSoundBuffer, false, 50);
}




bool LaserCannon::LaserBoltProjectile::_update(Map& map, float dt, bool exploded) {


    if(exploded) return exploded;


    //checkInitialSelfCollision();

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter) continue;

        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY * 0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY * 0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            createExplosion(map, x, y, characters, laserBoltExplosionRadius, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
        }*/
        if(map.characters[i]->intersects(x, y)) {
            createExplosion(map, x, y, characters, laserBoltExplosionRadius, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle) continue;

        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            createExplosion(map, x, y, characters, laserBoltExplosionRadius, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }
    

    bool collided = map.checkCollision(x, y);
    if(collided) {
        createExplosion(map, x, y, characters, laserBoltExplosionRadius, 1000, 0, 200);
        exploded = true;
        canBeRemoved = true;
    }

    return exploded;
}



void LaserCannon::LaserBoltProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer);

    //flameSoundInstance->removeWithFadeOut(1.0);
    //flameSoundInstance = nullptr; // TODO check this!

    for(int i=0; i<map.characters.size(); i++) {
        float dx = map.characters[i]->x - x;
        float dy = map.characters[i]->y - y;

        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            map.characters[i]->takeDamage(laserBoltDamage, 1, 1, 0, 0);
        }
    }
    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->checkCirclePixelCollision(x, y, explosionRadius)) {
            vehicles[i]->takeDamage(laserBoltDamage);
        }
    }


    float rr = explosionRadius * explosionRadius;
    float rrPlus1 = (explosionRadius+1) * (explosionRadius+1);

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius-1; i<explosionRadius+1; i++) {
        for(int j=-explosionRadius-1; j<explosionRadius+1; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
            else if(trr < rrPlus1) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                }
            }
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //int r = randi(100, 255);
        //int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        
        int alpha = randi(100, 255);
        explosionProjectile->color = sf::Color(255, 0, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 1.5;
        projectiles.push_back(explosionProjectile);
    }

}













void ClusterMortar::use(float x, float y, float vx, float vy, float angle) {

    
    float m = 0;
    if(itemUserCharacter) {
        m = max(itemUserCharacter->w*itemUserCharacter->scaleX, itemUserCharacter->h*itemUserCharacter->scaleY) * 0.5;
    }
    else if(itemUserVehicle) {
        m = max(itemUserVehicle->w*itemUserVehicle->scaleX, itemUserVehicle->h*itemUserVehicle->scaleY) * 0.5;
    }
    for(int i=0; i<numClusters; i++) {
        ClusterProjectile *clusterProjectile = new ClusterProjectile();
        clusterProjectile->x = x + m * cos(angle);
        clusterProjectile->y = y + m * sin(angle);
        float v = randf2(0.9, 1.1);
        float deltaAngle = randf2(-0.02*Pi, 0.02*Pi);
        clusterProjectile->vx = v * throwingVelocity * cos(angle+deltaAngle);
        clusterProjectile->vy = v * throwingVelocity * sin(angle+deltaAngle);
        clusterProjectile->gravityHasAnEffect = true;
        //clusterProjectile->characters = characters;
        clusterProjectile->projectileUserCharacter = itemUserCharacter;
        clusterProjectile->projectileUserVehicle = itemUserVehicle;
        projectiles.push_back(clusterProjectile);
    }

    soundWrapper.playSoundBuffer(clusterMortarSoundBuffer, false, 100);
}




bool ClusterMortar::ClusterProjectile::_update(Map& map, float dt, bool exploded) {
    if(exploded) return exploded;

    //float m = max(vx * dt, vy * dt);
    /*if(m > 1) {
        printf("m %f\n", m);
    }*/

    checkInitialSelfCollision();

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter && initialSelfCollision) continue;

        /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY * 0.5;
        float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX * 0.5;
        float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY * 0.5;

        if(isWithinRect(x, y, ax, ay, bx, by)) {
            createExplosion(map, x, y, characters, explosionRadius, 500, 0, 200);
            exploded = true;
            canBeRemoved = true;
        }*/
        if(map.characters[i]->intersects(x, y)) {
            createExplosion(map, x, y, characters, explosionRadius, 500, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle && initialSelfCollision) continue;
        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            createExplosion(map, x, y, characters, explosionRadius, 500, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true; // TODO check this!
        }
    }

    bool collided = map.checkCollision(x, y);
    if(collided) {
        createExplosion(map, x, y, characters, explosionRadius, 500, 0, 200);
        exploded = true;
        canBeRemoved = true;
    }

    return exploded;
}




void ClusterMortar::ClusterProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, /*ExplosionProjectile::Direction dir,*/ float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer);

    for(int i=0; i<map.characters.size(); i++) {
        float dx = map.characters[i]->x - x;
        float dy = map.characters[i]->y - y;

        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            map.characters[i]->takeDamage(damage, 1, 1, 0, 0);
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->checkCirclePixelCollision(x, y, explosionRadius)) {
            vehicles[i]->takeDamage(damage);
        }
    }

    float rr = explosionRadius * explosionRadius;
    float rrPlus1 = (explosionRadius+1) * (explosionRadius+1);

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius-1; i<explosionRadius+1; i++) {
        for(int j=-explosionRadius-1; j<explosionRadius+1; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
            else if(trr < rrPlus1) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                }
            }
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    /*for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 1.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }*/

    createExplosion2(x, y, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);
}















void SpawnVehicle::update(Map &map, float dt) {
    if(active) {
        if(vehicleType == VehicleType::Walker) {
            Walker *spawnedWalker = new Walker();
            spawnedWalker->setup(&map, "walker3", -1);
            vehicles.push_back(spawnedWalker); 
            soundWrapper.playSoundBuffer(spawnVehicleSoundBuffer, false, 100);
        }

        active = false;
    }
}
















void SpawnRobotGroup::update(Map &map, float dt) {
    if(active) {
        for(int i=0; i<numRobotsToSpawn; i++) {
            Robot *spawnedRobot = new Robot();
            if(itemUserCharacter && itemUserCharacter->colorTheme == ColorTheme::Blue) {
                spawnedRobot->setup(&map, "robot2_blue/robot", 1);
            }
            else if(itemUserCharacter && itemUserCharacter->colorTheme == ColorTheme::Red) {
                spawnedRobot->setup(&map, "robot2_red/robot", 2);
            }
            else {
                spawnedRobot->setup(&map, "robot2/robot", -1);
            }
            spawnedRobot->x = randf2(50, map.w*map.scaleX - 50);
            spawnedRobot->y = randf2(50, map.h*map.scaleY - 50);
            spawnedRobot->driverCharacter = itemUserCharacter;
            spawnedRobot->computerControl.isActive = true;
            spawnedRobot->remoteControlActive = true;
            vehicles.push_back(spawnedRobot); 
        }
        soundWrapper.playSoundBuffer(spawnRobotGroupSoundBuffer, false, 100);

        active = false;
    }
}











void FireBall::use(float x, float y, float vx, float vy, float angle) {
        float m = 0;
        if(itemUserCharacter) {
            m = max(itemUserCharacter->w*itemUserCharacter->scaleX, itemUserCharacter->h*itemUserCharacter->scaleY) * 0.5;
        }
        else if(itemUserVehicle) {
            m = max(itemUserVehicle->w*itemUserVehicle->scaleX, itemUserVehicle->h*itemUserVehicle->scaleY) * 0.5;
        }

        for(int i=0; i<numFireBalls; i++) {
            FireBallProjectile *fireBallProjectile = new FireBallProjectile();
            fireBallProjectile->x = x + m * cos(angle);
            fireBallProjectile->y = y + m * sin(angle);;
            float v = randf2(throwingVelocityMin, throwingVelocityMax);
            float a = angle + randf2(-deltaAngle, deltaAngle);
            fireBallProjectile->vx = v * cos(a);
            fireBallProjectile->vy = v * sin(a);
            fireBallProjectile->gravityHasAnEffect = true;

            fireBallProjectile->projectileUserCharacter = itemUserCharacter;
            fireBallProjectile->projectileUserVehicle = itemUserVehicle;
            projectiles.push_back(fireBallProjectile);
        }
    }



void FireBall::FireBallProjectile::createExplosion(Map& map, float x, float y, vector<Character*> &characters, /*ExplosionProjectile::Direction dir,*/ float explosionRadius, int numExplosionProjectiles, float explosionProjectileVelocityMin, float explosionProjectileVelocityMax) {

    soundWrapper.playSoundBuffer(explosionSoundBuffer);

    for(int i=0; i<map.characters.size(); i++) {
        float dx = map.characters[i]->x - x;
        float dy = map.characters[i]->y - y;

        float dist = sqrt(dx*dx + dy*dy);

        if(dist <= explosionRadius) {
            map.characters[i]->takeDamage(15, 1, 1, 0, 0);
        }
    }
    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i]->checkCirclePixelCollision(x, y, explosionRadius)) {
            vehicles[i]->takeDamage(15);
        }
    }

    float rr = explosionRadius * explosionRadius;

    vector<Map::Tile> explodedTiles;

    for(int i=-explosionRadius; i<explosionRadius; i++) {
        for(int j=-explosionRadius; j<explosionRadius; j++) {
            float trr = i*i + j*j;
            if(trr < rr) {
                float tx = map.mapX(x+i, screenW);
                float ty = map.mapY(y+j, screenH);
                if(map.isTileWithin(tx, ty) && map.tiles[tx + ty*map.w].type != Map::Tile::None) {
                    if(map.tiles[tx + ty*map.w].flammable) {
                        map.tiles[tx + ty*map.w].burning = true;
                    }
                    explodedTiles.push_back(map.tiles[tx + ty*map.w]);
                    map.tiles[tx + ty*map.w] = map.emptyTile;
                }
            }
        }
    }

    for(int i=0; i<explodedTiles.size(); i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float r = randf(0, explosionRadius);
        explosionProjectile->x = x + r * cos(angle);
        explosionProjectile->y = y + r * sin(angle);
        //explosionProjectile->x = x + 1 * 1.0/60 * cos(angle);
        //explosionProjectile->y = y + 1 * 1.0/60 * sin(angle);
        explosionProjectile->gravityHasAnEffect = true;
        explosionProjectile->type = ExplosionProjectile::Type::FlyingTile;
        explosionProjectile->flyingTile.tile = explodedTiles[i];
        if(explosionProjectile->flyingTile.tile.type == Map::Tile::Type::Ground) {
            explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        }
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //if(explosionProjectile->vy > 0) {
        //    explosionProjectile->direction = ExplosionProjectile::Direction::Up;
        //}
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }
//numExplosionProjectiles = 0;
    /*for(int i=0; i<numExplosionProjectiles; i++) {
        ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
        float angle = randf(0, 2.0*Pi);
        float v = randf(explosionProjectileVelocityMin, explosionProjectileVelocityMax);
        explosionProjectile->vx = v * cos(angle);
        explosionProjectile->vy = v * sin(angle);
        float angle2 = randf(0, 2.0*Pi);
        float ra = randf(0, explosionRadius);
        explosionProjectile->x = x + ra * cos(angle2);
        explosionProjectile->y = y + ra * sin(angle2);
        explosionProjectile->gravityHasAnEffect = false;
        explosionProjectile->type = ExplosionProjectile::Type::Basic;
        //float r = randi(50, 255);
        //float g = min(randi(0, 255), r);
        int r = randi(100, 255);
        int g = (int)mapf(ra, 0, explosionRadius, 0, randi(0, 255));
        g = min(g, r);
        int alpha = randi(50, 255);
        explosionProjectile->color = sf::Color(r, g, 0, alpha);
        explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
        explosionProjectile->duration = 1.5;
        //explosionProjectile->color3 = sf::Color(0, 0, 0, 0);
        //explosionProjectile->flyingTile.tile.type = Map::Tile::Type::Dirt;
        //explosionProjectile->flyingTile.tile.color = sf::Color(255, 255, 255, 255);
        //explosionProjectile->direction = dir;
        //explosionProjectile->characters = characters;
        //explosionProjectile->projectileUserCharacter = itemUserCharacter;
        projectiles.push_back(explosionProjectile);
    }*/

    createExplosion2(x, y, explosionRadius, numExplosionProjectiles, explosionProjectileVelocityMin, explosionProjectileVelocityMax);
}








bool FireBall::FireBallProjectile::_update(Map& map, float dt, int m, bool exploded) {

    if(exploded) return exploded;



    checkInitialSelfCollision();

    for(int i=0; i<map.characters.size(); i++) {
        if(map.characters[i] == projectileUserCharacter && initialSelfCollision) continue;
        if(map.characters[i]->intersects(x, y)) {
            createExplosion(map, x, y, characters, 150, 2000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true;
        }
    }

    for(int i=0; i<vehicles.size(); i++) {
        if(vehicles[i] == projectileUserVehicle && initialSelfCollision) {
            continue;
        }
        if(vehicles[i]->checkPixelPixelCollision(x, y)) {
            createExplosion(map, x, y, characters, 150, 2000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true;
        }
    }


    float rx = 0, ry = 0;

    float d = sqrt(vx*vx + vy*vy);
    if(d == 0) d = 1;
    bool collided = map.getCollisionReflection(x, y, vx/d, vy/d, rx, ry);

    if(collided && rx < 1000 && ry < 1000) {    // TODO fix that hack

        soundWrapper.playSoundBuffer(collisionSoundBuffer, false, 50);
        x += rx;
        y += ry;
        //vx = rx*d*0.999;
        //vy = ry*d*0.999;
        vx = rx*d*0.9;
        vy = ry*d*0.9;

        gravityHasAnEffect = false;
        float d = sqrt(vx*vx + vy*vy);
        if(d < 1) {
            vx = 0;
            vy = 0;
        }
        numBounces++;
        if(numBounces >= maxBounces) {
            createExplosion(map, x, y, characters, 75, 1000, 0, 200);
            exploded = true;
            canBeRemoved = true;
            return true;
        }
    }
    else {
        gravityHasAnEffect = true;
    }


    //createSmoke(x, y, map.characters, 1, 20, 400);
    //createFlame(x, y, map.characters, 1, 20, 400, 0);

    for(int i=0; i<1; i++) {
        FireBallFireProjectile *p = new FireBallFireProjectile();
        float d = randf2(0, 10);
        float a1 = randf2(0, 2.0*Pi);
        p->x = x + d * cos(a1);
        p->y = y + d * sin(a1);

        float v = randf2(10, 200);
        float a2 = randf2(0, 2.0*Pi);
        p->vx = 0.5*vx + v * cos(a2);
        p->vy = 0.5*vy + v * sin(a2);

        float alpha1 = randf2(150, 255);
        float g1 = randf2(0, 255);
        float b1 = randf2(0, g1*0.5);
        p->color1 = sf::Color(255, g1, b1, alpha1);

        float c = randf2(50, 150);
        float alpha2 = randf2(100, 150);
        p->color2 = sf::Color(c, c, c, alpha2);

        float c2 = randf2(0, 50);
        float alpha3 = randf2(0, 50);
        p->color3 = sf::Color(c2, c2, c2, alpha3);

        /*float g2 = randf2(50, 150);
        float r2 = randf2(g2, 255);
        float alpha2 = randf2(50, 150);
        p->color2 = sf::Color(r2, g2, 0, alpha2);

        float r3 = randf2(0, 50);
        float alpha3 = randf2(0, 50);
        p->color3 = sf::Color(r3, 0, 0, alpha3);*/

        p->duration1 = randf2(0.1, 0.3);
        p->duration2 = randf2(0.7, 1.4);
        
        p->gravityHasAnEffect = false;
        p->risingAcceleration = randf2(50, 1500);

        projectiles.push_back(p);
    }


    return false;
}




















void ExplosionProjectile::update(Map& map, float dt) {
    if(gravityHasAnEffect) {
        vy += gravity * dt;
    }
    x += vx * dt;
    y += vy * dt;

    time += dt;

    if(time >= duration) {
        canBeRemoved = true;
        return;
    }

    if(createSmokeTrail) {
        createSmoke(x, y, map.characters, 1, 20, 200);
        createFlame(x, y, map.characters, 1, 20, 200, 0);
    }

    if(dealDamage > 0) {
        for(int i=0; i<map.characters.size(); i++) {
            /*float ax = map.characters[i]->x - map.characters[i]->w*map.characters[i]->scaleX * 0.5;
            float ay = map.characters[i]->y - map.characters[i]->h*map.characters[i]->scaleY * 0.5;
            float bx = map.characters[i]->x + map.characters[i]->w*map.characters[i]->scaleX * 0.5;
            float by = map.characters[i]->y + map.characters[i]->h*map.characters[i]->scaleY * 0.5;

            if(isWithinRect(x, y, ax, ay, bx, by)) {
                map.characters[i]->takeDamage(dealDamage, 1, 1, 0, 0);
                canBeRemoved = true;
                return;
            }*/
            if(map.characters[i]->intersects(x, y)) {
                map.characters[i]->takeDamage(dealDamage, 1, 1, 0, 0);
                canBeRemoved = true;
                return;
            }
        }
        for(int i=0; i<vehicles.size(); i++) {
            if(vehicles[i]->checkPixelPixelCollision(x, y)) {
                vehicles[i]->takeDamage(0.2*dealDamage);
                canBeRemoved = true;
                return;
            }
        }
    }

    //if(type == Type::FlyingTile) {


        if(x >= map.mx_ + map.mw_) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(map.mx_ + map.mw_ - 1, screenW);
                int py = map.mapY(y, screenH);
                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }

            canBeRemoved = true;
        }
        else if(x < map.mx_) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(map.mx_+1, screenW);
                int py = map.mapY(y, screenH);
                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }
            canBeRemoved = true;
        }
        else if(y > map.my_ + map.mh_) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(x, screenW);
                int py = map.mapY(map.my_ + map.mh_ - 1, screenH);

                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }
            canBeRemoved = true;
        }
        else if(y < map.my_) {
            if(type == Type::FlyingTile) {
                int px = map.mapX(x, screenW);
                int py = map.mapY(map.my_+1, screenH);;

                if(px != -1 && py != -1) {
                    if(map.tiles[px + py*map.w].type == Map::Tile::Type::None) {
                        map.tiles[px + py*map.w] = flyingTile.tile;
                    }
                }
            }
            canBeRemoved = true;
        }
        else if(type == Type::FlyingTile || type == Type::BasicWithCollision) { // check the "if"
            int px = map.mapX(x, screenW);
            int py = map.mapY(y, screenH);
            if(px != -1 && py != -1) {
                if(map.tiles[px + py*map.w].type == Map::Tile::Ground || map.tiles[px + py*map.w].type == Map::Tile::Dirt) {
                    if(type == Type::FlyingTile) {
                        int distUp = 0, distDown = 0, distLeft = 0, distRight = 0;

                        while(map.isTileWithin(px, py-distUp) && (map.tiles[px + (py-distUp)*map.w].type == Map::Tile::Ground || map.tiles[px + (py-distUp)*map.w].type == Map::Tile::Dirt)) {
                            distUp++;
                        }
                        while(map.isTileWithin(px, py+distDown) && (map.tiles[px + (py+distDown)*map.w].type == Map::Tile::Ground || map.tiles[px + (py+distDown)*map.w].type == Map::Tile::Dirt)) {
                            distDown++;
                        }
                        while(map.isTileWithin(px-distLeft, py) && (map.tiles[px-distLeft + py*map.w].type == Map::Tile::Ground || map.tiles[px-distLeft + py*map.w].type == Map::Tile::Dirt)) {
                            distLeft++;
                        }
                        while(map.isTileWithin(px+distRight, py) && (map.tiles[px+distRight + py*map.w].type == Map::Tile::Ground || map.tiles[px+distRight + py*map.w].type == Map::Tile::Dirt)) {
                            distRight++;
                        }

                        if(distLeft <= distUp && distLeft <= distDown && distLeft <= distRight) {
                            direction = Direction::Left;
                        }
                        else if(distRight <= distUp && distLeft <= distDown && distRight <= distLeft) {
                            direction = Direction::Right;
                        }
                        else if(distDown <= distUp) {
                            direction = Direction::Down;
                        }
                        else {
                            direction = Direction::Up;
                        }

                        if(direction == Direction::Up) {
                            if(map.isTileWithin(px, py-distUp)) {
                                map.tiles[px + (py-distUp)*map.w] = flyingTile.tile;
                            }

                        }
                        else if(direction == Direction::Down) {
                            if(map.isTileWithin(px, py+distDown)) {
                                map.tiles[px + (py+distDown)*map.w] = flyingTile.tile;
                            }
                        }
                        if(direction == Direction::Left) {
                            if(map.isTileWithin(px-distLeft, py)) {
                                map.tiles[px-distLeft + py*map.w] = flyingTile.tile;
                            }

                        }
                        else if(direction == Direction::Right) {
                            if(map.isTileWithin(px+distRight, py)) {
                                map.tiles[px+distRight + py*map.w] = flyingTile.tile;
                            }
                        }


                    }
                    canBeRemoved = true;
                }
            }
        }
    //}

}







void Walker::enterThisVehicle(const std::vector<Character*> &characters) {
    renderEnterHalo = false;

    if(driverCharacter) {
        return;
    }

    for(int i=0; i<characters.size(); i++) {

        float dx = x - characters[i]->x;
        float dy = y - characters[i]->y;
        float dd = dx*dx + dy*dy;
        float r = max(w*scaleX, h*scaleY) * 0.75;
        bool withinRadius = dd < r * r;
        if(withinRadius) {
            if(characters[i]->doubleClickedItemChange) {
                driverCharacter = characters[i];
                characters[i]->inThisVehicle = this;
                characters[i]->doubleClickedItemChange = false;
                characters[i]->x = -12345;  // TODO fix this hack!
                characters[i]->y = -12345;
                characters[i]->stopUseItem();
                characters[i]->stopUseItemSecondary();
                characters[i]->afterUpdate(true);
            }
        }
        renderEnterHalo = renderEnterHalo || withinRadius;
        
        if(driverCharacter) {
            break;
        }
    }
}


void Walker::exitThisVehicle() {
    if(driverCharacter) {
        driverCharacter->inThisVehicle = nullptr;
        driverCharacter->x = x;
        driverCharacter->y = y;
        doubleClickedItemChange = false;
        driverCharacter = nullptr;
    }
}


void Walker::takeDamage(float amount) {
    //if(respawning) {
    //    return;
    //}
    if(hp <= 0) {
        return;
    }

    hp -= amount;
    if(hp <= 0) {
        hp = 0;
        vx = 0;
        vy = 0;
        canBeRemoved = true;

        if(driverCharacter) {
            driverCharacter->inThisVehicle = nullptr;
            driverCharacter->x = x;
            driverCharacter->y = y;
            driverCharacter->takeDamage(100, 0, 0, 0, 0);
            driverCharacter = nullptr;
        }

        soundWrapper.playSoundBuffer(walkerExplosionSoundBuffer);

        for(int i=0; i<4000; i++) {
            ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
            float angle = randf(0, 2.0*Pi);
            float v = randf(50, 600);
            explosionProjectile->vx = v * cos(angle);
            explosionProjectile->vy = v * sin(angle);
            float angle2 = randf(0, 2.0*Pi);
            float ra = randf(0, 100);
            explosionProjectile->x = x + ra * cos(angle2);
            explosionProjectile->y = y + ra * sin(angle2);
            explosionProjectile->gravityHasAnEffect = false;
            explosionProjectile->type = ExplosionProjectile::Type::Basic;
            int r = randi(100, 255);
            int g = (int)mapf(ra, 0, 100, 0, randi(0, 255));
            g = min(g, r);
            int alpha = randi(100, 255);
            explosionProjectile->color = sf::Color(r, g, 0, alpha);
            explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
            explosionProjectile->duration = 2.5;
            projectiles.push_back(explosionProjectile);
        }
        
        for(int i=0; i<items.size(); i++) {
            items[i]->afterUpdate(true);
        }
        for(int i=0; i<itemsSecondary.size(); i++) {
            itemsSecondary[i]->afterUpdate(true);
        }

        /*respawning = true;
        pointOfDeathX = x;
        pointOfDeathY = y;
        respawnX = randf2(50, screenW - 50);
        respawnY = randf2(50, screenH - 50);

        aiming = 0;

        if(items[6]->noManaCostPerUse()) {
            items[6]->use(0, 0, 0, 0, 0);
        }
        //else {
        //}
        stopUseItem();

        stopUseItemSecondary();
        afterUpdate(true);*/

        /*if(computerControl.isActive) {
            computerControl.onDeath();
        }*/
    }
}









void Robot::takeDamage(float amount) {
    //if(respawning) {
    //    return;
    //}
    if(hp <= 0) {
        return;
    }

    hp -= amount;
    if(hp <= 0) {
        hp = 0;
        vx = 0;
        vy = 0;
        canBeRemoved = true;

        /*if(driverCharacter) {
            driverCharacter->inThisVehicle = nullptr;
            driverCharacter->x = x;
            driverCharacter->y = y;
            driverCharacter->takeDamage(100, 0, 0, 0, 0);
            driverCharacter = nullptr;
        }*/

        soundWrapper.playSoundBuffer(robotExplosionSoundBuffer);

        for(int i=0; i<2000; i++) {
            ExplosionProjectile *explosionProjectile = new ExplosionProjectile();
            float angle = randf(0, 2.0*Pi);
            float v = randf(50, 300);
            explosionProjectile->vx = v * cos(angle);
            explosionProjectile->vy = v * sin(angle);
            float angle2 = randf(0, 2.0*Pi);
            float ra = randf(0, 40);
            explosionProjectile->x = x + ra * cos(angle2);
            explosionProjectile->y = y + ra * sin(angle2);
            explosionProjectile->gravityHasAnEffect = true;
            explosionProjectile->type = ExplosionProjectile::Type::Basic;
            //int r = randi(100, 255);
            //int g = (int)mapf(ra, 0, 10, 0, randi(0, 255));
            //g = min(g, r);
            int k = randi(0, 255);
            int alpha = randi(100, 255);
            explosionProjectile->color = sf::Color(k, k, k, alpha);
            explosionProjectile->color2 = sf::Color(0, 0, 0, 0);
            explosionProjectile->duration = randf2(0.1, 2.0);
            projectiles.push_back(explosionProjectile);
        }
        
        for(int i=0; i<items.size(); i++) {
            items[i]->afterUpdate(true);
        }
        for(int i=0; i<itemsSecondary.size(); i++) {
            itemsSecondary[i]->afterUpdate(true);
        }

        /*respawning = true;
        pointOfDeathX = x;
        pointOfDeathY = y;
        respawnX = randf2(50, screenW - 50);
        respawnY = randf2(50, screenH - 50);

        aiming = 0;

        if(items[6]->noManaCostPerUse()) {
            items[6]->use(0, 0, 0, 0, 0);
        }
        //else {
        //}
        stopUseItem();

        stopUseItemSecondary();
        afterUpdate(true);*/

        /*if(computerControl.isActive) {
            computerControl.onDeath();
        }*/
    }
}












void prepareProjectiles() {
    Bomb::BombProjectile::prepare();
    ExplosionProjectile::prepare();
    BloodProjectile::prepare();
    Smoke::prepare();
    Flame::prepare();
    ClusterBomb::ClusterBombProjectile::prepare();
    ClusterBomb::ClusterProjectile::prepare();
    Napalm::NapalmProjectile::prepare();
    Napalm::NapalmFireProjectile::prepare();
    Repeller::RepellerProjectile::prepare();
    FireProjectile::prepare();
    LightningStrikeProjectile::prepare();
    LandGrower::LandGrowerProjectile::prepare();
    Digger::DiggerProjectile::prepare();
    JetPack::JetPackProjectile::prepare();
    Blaster::BlasterProjectile::prepare();
    NuclearBomb::NuclearBombProjectile::prepare();
    MissileLauncher::MissileProjectile::prepare();
    MissileLauncher::MissileSmokeProjectile::prepare();
    LaserSight::LaserSightProjectile::prepare();
    ReflectorBeam::ReflectorBeamProjectile::prepare();
    BouncyBomb::BouncyBombProjectile::prepare();
    Rifle::RifleProjectile::prepare();
    Rifle::RifleHitProjectile::prepare();
    //Shotgun?
    DoomsDay::DoomsDayProjectile::prepare();
    DoomsDay::DoomsDaySmokeProjectile::prepare();
    Bolter::BoltProjectile::prepare();
    Bolter::BolterSmokeProjectile::prepare();
    Railgun::RailgunSmokeProjectile::prepare();
    Railgun::RailgunProjectile::prepare();
    LaserCannon::LaserBoltProjectile::prepare();
    ClusterMortar::ClusterProjectile::prepare();
    FireBall::FireBallProjectile::prepare();
}

void updateProjectiles(Map &map, float dt) {
    for(int i=projectiles.size()-1 ; i>=0; i--) {
        if(projectiles[i]->canBeRemoved) {
            delete projectiles[i];
            projectiles.erase(projectiles.begin()+i);
        }
    }
    for(int i=0; i<projectiles.size(); i++) {
        projectiles[i]->update(map, dt);
    }
}

void renderProjectiles(sf::RenderWindow &window, float scaleX, float scaleY) {
    for(int i=0; i<projectiles.size(); i++) {
        if((!areProjectilesPixelated && debugRenderProjectiles) || projectiles[i]->isRenderSprite()) {
            projectiles[i]->render(window, scaleX, scaleY);
        }
    }
}





void updateVehicles(float dt, int screenW, int screenH, Map &map) {
    for(int i=vehicles.size()-1 ; i>=0; i--) {
        if(vehicles[i]->canBeRemoved) {
            delete vehicles[i];
            vehicles.erase(vehicles.begin()+i);
        }
    }
    for(int i=0; i<vehicles.size(); i++) {
        vehicles[i]->update(dt, screenW, screenH, map);
    }
}

void renderVehicles(sf::RenderWindow &window) {
    for(int i=0; i<vehicles.size(); i++) {
        vehicles[i]->render(window);
    }
    for(int i=0; i<vehicles.size(); i++) {
        vehicles[i]->render(window);
    }
}






int main() {
    
    //VALGRIND_DISABLE_ERROR_REPORTING;
    //CALLGRIND_STOP_INSTRUMENTATION;

    srand(time(NULL));

    int n = std::thread::hardware_concurrency();
    if(n > 0) {
        printf("Number of hardware cores: %d\n", n);
        numCores = n;
    }
    else {
        printf("Couldn't determine the number of hardware cores.\n");
    }

    Mouse mouse;


    std::vector<sf::VideoMode> modes = sf::VideoMode::getFullscreenModes();
    for(std::size_t i = 0; i < modes.size(); i++) {
        sf::VideoMode mode = modes[i];
        std::cout << "Mode #" << i << ": "
                  << mode.width << "x" << mode.height << " - "
                  << mode.bitsPerPixel << " bpp" << std::endl;
    }

    sf::ContextSettings settings;
    settings.antialiasingLevel = 0;

    sf::RenderWindow window(modes[0], "Bang Bang", sf::Style::Fullscreen);
    //sf::RenderWindow window(sf::VideoMode(screenW, screenH), "Animated pixels", sf::Style::Default, settings);
        //sf::Style::Titlebar | sf::Style::Close);

    sf::Vector2u size = window.getSize();
    //w = size.x;
    //h = size.y;
    screenW = size.x;
    screenH = size.y;



    window.setVerticalSyncEnabled(true);

    sf::Font font;
    if (!font.loadFromFile("data/fonts/georgia/Georgia.ttf")) {
        printf("Failed to load font 'data/fonts/georgia/Georgia.ttf'\n");
    }
    /*if(!font.loadFromFile("data/fonts/8bitOperatorPlus8-Regular.ttf")) {
        printf("Failed to load font 'data/fonts/8bitOperatorPlus8-Regular.ttf'\n");
    }*/
    /*if (!font.loadFromFile("data/fonts/Starmap/00TT.TTF")) {
        printf("Failed to load font 'data/fonts/Starmap/00TT.TTF'\n");
    }*/


    sf::Text fpsText;
    fpsText.setFont(font);
    fpsText.setString("Hello SFML");
    fpsText.setCharacterSize(16);
    fpsText.setFillColor(sf::Color::White);

    sf::RectangleShape fpsTextRect;
    fpsTextRect.setFillColor(sf::Color(0, 0, 0, 100));
    fpsTextRect.setOutlineColor(sf::Color(0, 0, 0, 100));
    fpsTextRect.setOutlineThickness(-1.0);

    sf::Text trackDetailsText;
    trackDetailsText.setFont(font);
    trackDetailsText.setString("Music starting soon...");
    trackDetailsText.setCharacterSize(64);
    trackDetailsText.setFillColor(sf::Color::White);

    sf::RectangleShape trackDetailsRect;
    trackDetailsRect.setFillColor(sf::Color(0, 0, 0, 100));
    trackDetailsRect.setOutlineColor(sf::Color(255, 255, 255, 100));
    trackDetailsRect.setOutlineThickness(3.0);

    /*sf::Text gameStatusText;
    gameStatusText.setFont(font);
    gameStatusText.setString("123");
    gameStatusText.setCharacterSize(30);
    gameStatusText.setFillColor(sf::Color::Red);*/

    sf::Text characterStatusText;
    characterStatusText.setFont(font);
    characterStatusText.setCharacterSize(16);
    characterStatusText.setFillColor(sf::Color(255, 255, 255, 200));

    sf::RectangleShape manaRect, hpRect, borderRect;
    hpRect.setFillColor(sf::Color(200, 0, 0, 100));
    manaRect.setFillColor(sf::Color(80, 80, 200, 100));
    borderRect.setFillColor(sf::Color(255, 255, 255, 0));
    borderRect.setOutlineColor(sf::Color(255, 255, 255, 100));
    borderRect.setOutlineThickness(-1.0);


    Map map(font, screenW/scaleX, screenH/scaleY, scaleX, scaleY);
    //Map map(font, 600, 300, scaleX, scaleY);
    //Map map(font, screenW/(scaleX*2), screenH/(scaleY*2), scaleX, scaleY);
    //map.occlusionRenderTextureW = 1200;

    Character character, character2;
    //character.setup(&map, "walker", screenW, screenH, map.scaleX, map.scaleY);
    //character2.setup(&map, "walker", screenW, screenH, map.scaleX, map.scaleY);

    character.setup(&map, "tyyppi", screenW, screenH, map.scaleX, map.scaleY, ColorTheme::Blue);
    character2.setup(&map, "tyyppi2", screenW, screenH, map.scaleX, map.scaleY, ColorTheme::Red);
    /*character.setup(&map, "data/textures/ukko.png", screenW, screenH, map.scaleX, map.scaleY);
    character2.setup(&map, "data/textures/ukko2.png", screenW, screenH, map.scaleX, map.scaleY);*/

    vector<Character*> characters;

    characters.push_back(&character);
    characters.push_back(&character2);

    character.addCharacters(characters);
    character2.addCharacters(characters);

    map.addCharacters(characters);

    character.computerControl.init(&map, &character, characters);
    character2.computerControl.init(&map, &character2, characters);

    prepareProjectiles();


    /*Walker *walkerTest = new Walker();
    walkerTest->setup(&map, "walker3");
    vehicles.push_back(walkerTest);*/


    MusicAlbum organWorks;
    organWorks.setup(time(NULL));
    organWorks.openAlbum("organ_works", "Lassi P.", 20, "ogg");
    organWorks.shufflePlayList();
    organWorks.play();


    //VALGRIND_ENABLE_ERROR_REPORTING;
    //CALLGRIND_START_INSTRUMENTATION;

    /*std::vector<float> tickTock;
    tickTock.resize(16);
    
    std::vector<std::vector<double>> avgTimes;
    avgTimes.resize(16);
    for(int i=0; i<16; i++) {
        avgTimes[i].resize(60 * 60 * 60 * 60);
    }
    long frameCounter = 0;
    double previousUpdateTime = 0;

    std::vector<std::string> profilingSegmentNames = {
        "poll events",
        "soundWrapper.update()",
        "character.update()",
        "updateVehicles()",
        "updateProjectiles()",
        "map.update()",
        "map.render()",
        "map.renderPixelProjectiles()",
        "map.renderFinal()",
        "renderVehicles()",
        "",
        "",
        "",
        "",
        "",
        "window.display()"};*/
        
                                                       


    while(window.isOpen()) {
        timer.update();

        //timer.tick();

        sf::Event event;
        while(window.pollEvent(event)) {
            if(event.type == sf::Event::Closed) {
                window.close();
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Escape) {
                window.close();
            }
            if (event.type == sf::Event::Resized) {
                std::cout << "new width: " << event.size.width << std::endl;
                std::cout << "new height: " << event.size.height << std::endl;
                window.setSize(sf::Vector2u(event.size.width, event.size.height));
                screenW = event.size.width;
                screenH = event.size.height;
            }
            /*if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F10) {
                organWorks.play();
            }*/


            /*if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Num1) {
                map.updateFrequencySeconds = 1.0/3.0;
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Num2) {
                map.updateFrequencySeconds = 0;
            }*/
            /*if(event.type == sf::Event::KeyPressed) {
                printf("Key: %d\n", event.key.code);
            }*/

            /*if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F12) {
                float totalTime = 0;
                for(int i=0; i<tickTock.size(); i++) {
                    totalTime += tickTock[i];
                }
                printf("#### %f seconds from start ####\n", timer.totalTime);
                double playTime = timer.totalTime - previousUpdateTime;
                printf("Recorded %f seconds of play time.\n", playTime);
                printf("The durations are the slowest of each kind.\n");
                for(int i=0; i<tickTock.size(); i++) {
                    float percentage = tickTock[i] / totalTime * 100.0;
                    float millis = tickTock[i] * 1000.0;
                    printf("#%d:\t%f ms\t%f %%", i, millis, percentage);
                    printf("\t%s\n", profilingSegmentNames[i].c_str());
                }
                printf("Total:\t%f ms\n", (totalTime * 1000.0));

                std::vector<double> avgTotals(16, 0.0);
                double avgTotal = 0;
                for(int i=0; i<16; i++) {
                    for(int k=0; k<frameCounter; k++) {
                        avgTotals[i] += avgTimes[i][k];
                    }
                    avgTotals[i] /= frameCounter;
                    avgTotal += avgTotals[i];
                }
                printf("The durations are the averages of each kind.\n");
                for(int i=0; i<16; i++) {
                    float percentage = avgTotals[i] / avgTotal * 100.0;
                    float millis = avgTotals[i] * 1000.0;
                    printf("#%d:\t%f ms\t%f %%", i, millis, percentage);
                    printf("\t%s\n", profilingSegmentNames[i].c_str());
                }
                printf("Total:\t%f ms\n", (avgTotal * 1000.0));

                for(int i=0; i<tickTock.size(); i++) {
                    tickTock[i] = 0;
                }

                frameCounter = 0;
                previousUpdateTime = timer.totalTime;
            }*/

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F4) {
                Walker *walkerTest = new Walker();
                walkerTest->setup(&map, "walker3", 0);
                vehicles.push_back(walkerTest); 
            }

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F5) {
                Robot *robotTest = new Robot();
                robotTest->setup(&map, "robot2/robot", -1);
                vehicles.push_back(robotTest); 
            }

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F6) {
                printf("Character 1. ");
                character.computerControl.printStatus();
                printf("Character 2. ");
                character2.computerControl.printStatus();
            }

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F7) {
                int n = projectiles.size();
                printf("Number of projectiles: %d\n", n);
                for(int i=0; i<projectiles.size(); i++) {
                    printf("#%d\t%s\tpos (%f, %f)\tvel (%f, %f)\n",
                        i, projectiles[i]->getName().c_str(),
                        projectiles[i]->x, projectiles[i]->y,
                        projectiles[i]->vx, projectiles[i]->vy);
                }
            }

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::R) {
                map.tilePainter.setTileType(0);
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::T) {
                map.tilePainter.setTileType(1);
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Y) {
                map.tilePainter.setTileType(2);
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::U) {
                map.tilePainter.setTileType(3);
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::I) {
                map.tilePainter.setTileType(4);
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::O) {
                map.tilePainter.flammable = !map.tilePainter.flammable;
            }

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Num1) {
                map.tilePainter.brushSize = 1;
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Num2) {
                map.tilePainter.brushSize = 3;
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Num3) {
                map.tilePainter.brushSize = 5;
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Num4) {
                map.tilePainter.brushSize = 9;
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Num5) {
                map.tilePainter.brushSize = 13;
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Num6) {
                map.tilePainter.brushSize = 21;
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Num7) {
                map.tilePainter.brushSize = 35;
            }

            /*if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F1) {
                map.fieldOfVision = Map::FieldOfVision((map.fieldOfVision+1) % 3);
            }*/
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F2) {
                character.computerControl.isActive = !character.computerControl.isActive;
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F3) {
                character2.computerControl.isActive = !character2.computerControl.isActive;
            }



            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Left) {
                character.moveLeft();
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Right) {
                character.moveRight();
            }
            if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Left) {
                character.stopMoveLeft();
            }
            if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Right) {
                character.stopMoveRight();
            }

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Up) {
                character.aimUp();
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Down) {
                character.aimDown();
            }
            if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Up) {
                character.stopAimUp();
            }
            if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Down) {
                character.stopAimDown();
            }

            //if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::RControl) {
            if(event.type == sf::Event::KeyPressed && event.key.code == 56) {
                character.jump();
            }
            //if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::RShift) {
            if(event.type == sf::Event::KeyPressed && event.key.code == 50) {
                character.useItem();
            }
            //if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::RShift) {
            if(event.type == sf::Event::KeyReleased && event.key.code == 50) {
                character.stopUseItem();
            }
            //if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Enter) {
            if(event.type == sf::Event::KeyPressed && event.key.code == 49) {
                character.useItemSecondary();
            }
            //if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Enter) {
            if(event.type == sf::Event::KeyReleased && event.key.code == 49) {
                character.stopUseItemSecondary();
            }
            //if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::BackSpace) {
            if(event.type == sf::Event::KeyPressed && event.key.code == 12) {
                character.changeItem();
            }
            //if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::BackSpace) {
            /*if(event.type == sf::Event::KeyReleased && event.key.code == 12) {
                character.stopChangeItem();
            }*/

            // TODO debug, LShift/RShift get stuck


            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::A) {
                character2.moveLeft();
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::D) {
                character2.moveRight();
            }
            if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::A) {
                character2.stopMoveLeft();
            }
            if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::D) {
                character2.stopMoveRight();
            }

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::W) {
                character2.aimUp();
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::S) {
                character2.aimDown();
            }
            if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::W) {
                character2.stopAimUp();
            }
            if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::S) {
                character2.stopAimDown();
            }

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::LControl) {
                character2.jump();
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::LShift) {
                character2.useItem();
            }

            if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::LShift) {
                character2.stopUseItem();
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Tab) {
                character2.useItemSecondary();
            }
            if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Tab) {
                character2.stopUseItemSecondary();
            }
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Q) {
                character2.changeItem();
            }
            /*if(event.type == sf::Event::KeyReleased && event.key.code == sf::Keyboard::Q) {
                character2.stopChangeItem();
            }*/


            if(event.type == sf::Event::MouseMoved) {
                mouse.x = event.mouseMove.x;
                mouse.y = event.mouseMove.y;
                map.hover(mouse.x, mouse.y, screenW, screenH);

                //int ix = 0, iy = 0;
                //bool hit = walkerTest->checkPixelPixelCollision(mouse.x, mouse.y, ix, iy);
                //bool hit = walkerTest->checkCirclePixelCollision(mouse.x, mouse.y, 8);
                /*if(hit) {
                    printf("Hit! %d, %d\n", ix, iy);
                }
                else {
                    printf("Didn't hit. %d, %d\n", ix, iy);
                }*/
            }
            if(event.type == sf::Event::MouseButtonPressed) {
                if(event.mouseButton.button == sf::Mouse::Button::Left) {
                    mouse.leftPressed = true;
                    map.click(true, mouse.x, mouse.y, screenW, screenH);
                }
                if(event.mouseButton.button == sf::Mouse::Button::Middle) {
                    mouse.middlePressed = true;
                }
                if(event.mouseButton.button == sf::Mouse::Button::Right) {
                    mouse.rightPressed = true;
                    map.click(false, mouse.x, mouse.y, screenW, screenH);
                }
            }
            if(event.type == sf::Event::MouseButtonReleased) {
                if(event.mouseButton.button == sf::Mouse::Button::Left) {
                    mouse.leftPressed = false;
                }
                if(event.mouseButton.button == sf::Mouse::Button::Middle) {
                    mouse.middlePressed = false;
                }
                if(event.mouseButton.button == sf::Mouse::Button::Right) {
                    mouse.rightPressed = false;
                }
            }
            /*if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Enter) {
                map.restart();
            }*/
            /*if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::G) {
                map.debugTileRenderMode = (map.debugTileRenderMode+1) % 3;
            }*/
            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::F) {
                map.useDebugColors = !map.useDebugColors;
            }
            /*if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::V) {
                debugRenderProjectiles = !debugRenderProjectiles;
            }*/
            /*if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::B) {
                areProjectilesPixelated = !areProjectilesPixelated;
                if(areProjectilesPixelated) printf("Projectiles pixelated!\n");
                if(!areProjectilesPixelated) printf("Projectiles as rectangles!\n");
            }*/
            /*if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::Z) {
                map.asd(map.tilePainter, map.asdCounter);
                map.asdCounter++;
                if(map.asdCounter > 255) map.asdCounter = 0;
            }*/

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::N) {
                float v = 1.0 - masterVolume;
                soundWrapper.setMasterVolume(v);
            }

            /*if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::H) {
                map.startEarthquake(0);
            }

            if(event.type == sf::Event::KeyPressed && event.key.code == sf::Keyboard::J) {
                map.startEarthquake(1);
            }*/

        }

        //float t0 = timer.tock();
        //tickTock[0] = max(t0, tickTock[0]);
        //avgTimes[0][frameCounter] = t0;

        //timer.tick();
        soundWrapper.update(timer.frameTime);
        organWorks.update();
        //float t1 = timer.tock();
        //tickTock[1] = max(t1, tickTock[1]);
        //avgTimes[1][frameCounter] = t1;

        //timer.tick();
        character.update(timer.frameTime, screenW, screenH, map);
        character2.update(timer.frameTime, screenW, screenH, map);
        //float t2 = timer.tock();
        //tickTock[2] = max(t2, tickTock[2]);
        //avgTimes[2][frameCounter] = t2;


        //timer.tick();
        updateVehicles(timer.frameTime, screenW, screenH, map);

        for(int i=0; i<vehicles.size(); i++) {
            vehicles[i]->enterThisVehicle(characters);
        }
        //float t3 = timer.tock();
        //tickTock[3] = max(t3, tickTock[3]);
        //avgTimes[3][frameCounter] = t3;

        //timer.tick();
        updateProjectiles(map, timer.frameTime);
        //float t4 = timer.tock();
        //tickTock[4] = max(t4, tickTock[4]);
        //avgTimes[4][frameCounter] = t4;


        window.clear();

        if(mouse.leftPressed || mouse.rightPressed) {
            map.drag(mouse.leftPressed, mouse.x, mouse.y, screenW, screenH);
        }

        //timer.tick();
        //if(timer.totalTime > 1) {
            map.update(timer.frameTime);
//        }
        //float t5 = timer.tock();
        //tickTock[5] = max(t5, tickTock[5]);
        //avgTimes[5][frameCounter] = t5;

        //map.renderBg(screenW, screenH, window);
        //timer.tick();
        map.render(screenW, screenH, window);
        //float t6 = timer.tock();
        //tickTock[6] = max(t6, tickTock[6]);
        //avgTimes[6][frameCounter] = t6;

        //duration = timer.tock();
        //printf("map.render() duration %f ms, %f \%\n", duration * 1000, duration / timer.frameTime * 100);

        //timer.tick();
        map.renderPixelProjectiles(screenW, screenH);
        //float t7 = timer.tock();
        //tickTock[7] = max(t7, tickTock[7]);
        //avgTimes[7][frameCounter] = t7;

        //timer.tick();
        map.renderFinal(screenW, screenH, window);
        //float t8 = timer.tock();
        //tickTock[8] = max(t8, tickTock[8]);
        //avgTimes[8][frameCounter] = t8;


        //timer.tick();
        renderVehicles(window);
        //float t9 = timer.tock();
        //tickTock[9] = max(t9, tickTock[9]);
        //avgTimes[9][frameCounter] = t9;

        //timer.tick();
        character.render(window);
        character2.render(window);
        //float t10 = timer.tock();
        //tickTock[10] = max(t10, tickTock[10]);
        //avgTimes[10][frameCounter] = t10;

        //timer.tick();
        renderProjectiles(window, map.scaleX, map.scaleY);
        //float t11 = timer.tock();
        //tickTock[11] = max(t11, tickTock[11]);
        //avgTimes[11][frameCounter] = t11;

        //timer.tick();
        character.renderCrosshair(window);
        character2.renderCrosshair(window);
        //float t12 = timer.tock();
        //tickTock[12] = max(t12, tickTock[12]);
        //avgTimes[12][frameCounter] = t12;



//        timer.tick();
        float barWidth = 80;
        float barHeight = 8;

        for(int i=0; i<vehicles.size(); i++) {
            vehicles[i]->renderCrosshair(window);

            if(vehicles[i]->itemChange == Vehicle::ItemChange::ItemChangePrimary) {
                characterStatusText.setString("Primary equipment:\n" + vehicles[i]->items[vehicles[i]->activeItem]->getName());
                characterStatusText.setPosition(vehicles[i]->x-barWidth*0.5, vehicles[i]->y - vehicles[i]->h*0.5*scaleY - 80);
                window.draw(characterStatusText);
            }

            if(vehicles[i]->itemChange == Vehicle::ItemChange::ItemChangeSecondary) {
                characterStatusText.setString("Secondary equipment:\n" + vehicles[i]->itemsSecondary[vehicles[i]->activeItemSecondary]->getName());
                characterStatusText.setPosition(vehicles[i]->x-barWidth*0.5, vehicles[i]->y - vehicles[i]->h*0.5*scaleY - 80);
                window.draw(characterStatusText);
            }

            hpRect.setPosition(vehicles[i]->x-barWidth*0.5, vehicles[i]->y - vehicles[i]->h*0.5*scaleY - 37);
            hpRect.setSize(sf::Vector2f(barWidth*vehicles[i]->hp/vehicles[i]->maxHp, barHeight));
            window.draw(hpRect);
            borderRect.setPosition(vehicles[i]->x-barWidth*0.5, vehicles[i]->y - vehicles[i]->h*0.5*scaleY - 37);
            borderRect.setSize(sf::Vector2f(barWidth, barHeight));
            window.draw(borderRect);

            manaRect.setPosition(vehicles[i]->x-barWidth*0.5, vehicles[i]->y - vehicles[i]->h*0.5*scaleY - 20);
            manaRect.setSize(sf::Vector2f(barWidth*vehicles[i]->items[vehicles[i]->activeItem]->itemMana/vehicles[i]->maxMana, barHeight));
            window.draw(manaRect);
            borderRect.setPosition(vehicles[i]->x-barWidth*0.5, vehicles[i]->y - vehicles[i]->h*0.5*scaleY - 20);
            borderRect.setSize(sf::Vector2f(barWidth, barHeight));
            window.draw(borderRect);
        
        }

        if(!character.respawning && !character.inThisVehicle) {
            if(character.itemChange == Character::ItemChange::ItemChangePrimary) {
                characterStatusText.setString("Primary equipment:\n" + character.items[character.activeItem]->getName());
                characterStatusText.setPosition(character.x-barWidth*0.5, character.y - character.h*0.5*scaleY - 80);
                window.draw(characterStatusText);
            }

            if(character.itemChange == Character::ItemChange::ItemChangeSecondary) {
                characterStatusText.setString("Secondary equipment:\n" + character.itemsSecondary[character.activeItemSecondary]->getName());
                characterStatusText.setPosition(character.x-barWidth*0.5, character.y - character.h*0.5*scaleY - 80);
                window.draw(characterStatusText);
            }

            hpRect.setPosition(character.x-barWidth*0.5, character.y - character.h*0.5*scaleY - 37);
            hpRect.setSize(sf::Vector2f(barWidth*character.hp/character.maxHp, barHeight));
            window.draw(hpRect);
            borderRect.setPosition(character.x-barWidth*0.5, character.y - character.h*0.5*scaleY - 37);
            borderRect.setSize(sf::Vector2f(barWidth, barHeight));
            window.draw(borderRect);

            manaRect.setPosition(character.x-barWidth*0.5, character.y - character.h*0.5*scaleY - 20);
            manaRect.setSize(sf::Vector2f(barWidth*character.items[character.activeItem]->itemMana/character.maxMana, barHeight));
            window.draw(manaRect);
            borderRect.setPosition(character.x-barWidth*0.5, character.y - character.h*0.5*scaleY - 20);
            borderRect.setSize(sf::Vector2f(barWidth, barHeight));
            window.draw(borderRect);
        }

        if(!character2.respawning && !character2.inThisVehicle) {
            if(character2.itemChange == Character::ItemChange::ItemChangePrimary) {
                characterStatusText.setString("Primary equipment:\n" + character2.items[character2.activeItem]->getName());
                characterStatusText.setPosition(character2.x-barWidth*0.5, character2.y - character2.h*0.5*scaleY - 80);
                window.draw(characterStatusText);
            }

            if(character2.itemChange == Character::ItemChange::ItemChangeSecondary) {
                characterStatusText.setString("Secondary equipment:\n" + character2.itemsSecondary[character2.activeItemSecondary]->getName());
                characterStatusText.setPosition(character2.x-barWidth*0.5, character2.y - character2.h*0.5*scaleY - 80);
                window.draw(characterStatusText);
            }

            hpRect.setPosition(character2.x-barWidth*0.5, character2.y - character2.h*0.5*scaleY - 37);
            hpRect.setSize(sf::Vector2f(barWidth*character2.hp/character2.maxHp, barHeight));
            window.draw(hpRect);
            borderRect.setPosition(character2.x-barWidth*0.5, character2.y - character2.h*0.5*scaleY - 37);
            borderRect.setSize(sf::Vector2f(barWidth, barHeight));
            window.draw(borderRect);

            manaRect.setPosition(character2.x-barWidth*0.5, character2.y - character2.h*0.5*scaleY - 20);
            manaRect.setSize(sf::Vector2f(barWidth*character2.items[character2.activeItem]->itemMana/character2.maxMana, barHeight));
            window.draw(manaRect);
            borderRect.setPosition(character2.x-barWidth*0.5, character2.y - character2.h*0.5*scaleY - 20);
            borderRect.setSize(sf::Vector2f(barWidth, barHeight));
            window.draw(borderRect);
        }
        //float t13 = timer.tock();
        //tickTock[13] = max(t13, tickTock[13]);
        //avgTimes[13][frameCounter] = t13;


        //character.collisionHandler.renderCollisionPoints(window, character);
        
        //timer.tick();

        
        float angle = character.aimAngle;
        if(character.inThisVehicle) {
            angle = character.inThisVehicle->aimAngle;
        }

        float gangle = character.globalAngle;
        if(character.inThisVehicle) {
            gangle = character.inThisVehicle->globalAngle;
        }

        float distCC = 0;
        float x1 = 0, y1 = 0;
        float x2 = 0, y2 = 0;
        if(character.inThisVehicle) {
            x1 = character.inThisVehicle->x;
            y1 = character.inThisVehicle->y;
        }
        else {
            x1 = character.x;
            y1 = character.y;
        }
        if(character2.inThisVehicle) {
            x2 = character2.inThisVehicle->x;
            y2 = character2.inThisVehicle->y;
        }
        else {
            x2 = character2.x;
            y2 = character2.y;
        }
        distCC = sqrt((x1-x2)*(x1-x2) + (y1-y2)*(y1-y2));

        std::string nowPlaying = organWorks.getCurrentlyPlayingPieceName();
        //float musicOffset = organWorks.getCurrentlyPlayingPieceOffsetSeconds();

        fpsText.setString("music: " + nowPlaying + "\n" +
                          //" - " + to_string(musicOffset) + " s" + "\n" +
                          "time " + to_string(timer.totalTime) + "\n" +
                          //"dt " + to_string(timer.frameTime) + "\n" +
                          "fps " + to_string(timer.fps)+ "\n" +
                          "num projectiles "+to_string(projectiles.size())+ "\n" +
                          "num sounds "+to_string(soundWrapper.soundVector.size()) +
                          //"\naim angle " + to_string(angle) + 
                          //"\nglobal angle " + to_string(gangle) +
                          "\ndist " + to_string(distCC));

        fpsTextRect.setPosition(0, 0);
        fpsTextRect.setSize(sf::Vector2f(250, 120));
        window.draw(fpsTextRect);

        window.draw(fpsText);

        /*std::string nowPlaying = organWorks.getCurrentlyPlayingPieceName();
        int trackOffset = organWorks.getCurrentlyPlayingPieceOffsetSeconds();
        int trackDuration = organWorks.getCurrentlyPlayingPieceDurationSeconds();

        std::string trackText = "Music starting soon...";

        if(nowPlaying.size() > 0) {
            trackText = nowPlaying;
            int minutesOffset = trackOffset / 60;
            int secondsOffset = trackOffset - minutesOffset * 60;    
            std::string offset = "0" + std::to_string(minutesOffset) + ":";
            if(secondsOffset < 10) {
                offset += "0";
            }
            offset += std::to_string(secondsOffset);

            int minutesDuration = trackDuration / 60;
            int secondsDuration = trackDuration - minutesDuration * 60;
            std::string duration = "0" + std::to_string(minutesDuration) + ":";
            if(secondsDuration < 10) {
                duration += "0";
            }
            duration += std::to_string(secondsDuration);

            trackText += "\n" + offset + " / " + duration;
        }
        if(!organWorks.isPlaying && organWorks.playListIndex > 0) {
            trackText = "Thanks for\nlistening!";
        }

        trackDetailsText.setString(trackText);
        trackDetailsText.setPosition( screenW/2 - 300, screenH/2-100);

        trackDetailsRect.setPosition(screenW/2 - 350, screenH/2 - 150);
        trackDetailsRect.setSize(sf::Vector2f(700, 300));
        window.draw(trackDetailsRect);

        window.draw(trackDetailsText);*/
        
        //float t14 = timer.tock();
        //tickTock[14] = max(t14, tickTock[14]);
        //avgTimes[14][frameCounter] = t14;

        /*sf::CircleShape cs;

        cs.setRadius(100);
        cs.setOrigin(100, 100);
        cs.setFillColor(sf::Color(255, 255, 255, 40));
        cs.setPosition(screenW*0.5, screenH);
        window.draw(cs);

        cs.setRadius(250);
        cs.setOrigin(250, 250);
        cs.setFillColor(sf::Color(255, 255, 255, 40));
        cs.setPosition(screenW*0.5, screenH);
        window.draw(cs);

        cs.setRadius(500);
        cs.setOrigin(500, 500);
        cs.setFillColor(sf::Color(255, 255, 255, 40));
        cs.setPosition(screenW*0.5, screenH);
        window.draw(cs);

        cs.setRadius(1000);
        cs.setOrigin(1000, 1000);
        cs.setFillColor(sf::Color(255, 255, 255, 40));
        cs.setPosition(screenW*0.5, screenH);
        window.draw(cs);*/

        //timer.tick();
        window.display();
        //float t15 = timer.tock();
        //tickTock[15] = max(t15, tickTock[15]);
        //avgTimes[15][frameCounter] = t15;

        //frameCounter++;
    }

    soundWrapper.finish();

    return 0;
}
