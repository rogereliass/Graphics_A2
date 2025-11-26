/********************************************************************************
 DMET 502 — Computer Graphics
 Assignment 2 — Space Station (Single-file submission)
 Student: Roger George
 Student ID: 58-0352
 File: P15-58-0352.cpp

 Final corrected version:
 - Center start (player at center)
 - Fixed camera views (1,2,3) to look at scene center, not walls
 - Start menu (press ENTER to begin)
 - Timer runs only during gameplay and triggers GAME LOSE properly
 - Goal collection triggers GAME WIN only if within game time
 - No GLUT_KEY_SHIFT references; vertical movement via SPACE / Q / E
 - All other assignment requirements included (primitives, animations, collisions ...)
********************************************************************************/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Windows must come before GL
#include <Windows.h>

// For sound system
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

// GLUT must come AFTER Windows
#include <GL/glut.h>


// -------------------------------------------------------------
//  SOUND CONTROL  (background music + one-shot sound effects)
// -------------------------------------------------------------
void playBackgroundMusic() {
    // plays and loops background.mp3 forever
    mciSendString(L"open \"background.mp3\" type mpegvideo alias bgm", NULL, 0, NULL);
    mciSendString(L"play bgm repeat", NULL, 0, NULL);
    // set volume (0–1000, lower = quieter)
    mciSendString(L"setaudio bgm volume to 300", NULL, 0, NULL);
}

void stopBackgroundMusic() {
    wchar_t status[128] = { 0 };
    mciSendString(L"status bgm mode", status, 128, NULL);
    if (wcscmp(status, L"playing") == 0 || wcscmp(status, L"paused") == 0) {
        mciSendString(L"stop bgm", NULL, 0, NULL);
    }
    mciSendString(L"close bgm", NULL, 0, NULL);
}


void playSoundEffect(const char* file) {
    wchar_t cmd[256];
    printf("Playing sound: %s\n", file);

    swprintf(cmd, 256, L"open \"%hs\" type waveaudio alias se", file);
    MCIERROR err = mciSendString(cmd, NULL, 0, NULL);
    if (err != 0) {
        wchar_t buf[256];
        mciGetErrorString(err, buf, 256);
        MessageBox(NULL, buf, L"Sound Error", MB_OK);
        return;
    }
    mciSendString(L"play se from 0 wait", NULL, 0, NULL);  // waits for clip to finish
    mciSendString(L"close se", NULL, 0, NULL);

}









// --------------------------- CONFIG & PRIMITIVE COUNTS -------------------------
const float PLAYER_SPEED = 4.0f;         // units/sec
const float AIR_TILT_DEG = 20.0f;        // degrees pitch when airborne
const int GAME_DURATION_SEC = 5;       // seconds
const int NUM_GOALS = 1;

// --------------------------- GLOBALS & STATE ---------------------------------
int windowWidth = 1000, windowHeight = 700;
int gameStartMillis = 0;
int gameDurationMillis = GAME_DURATION_SEC * 1000;
float wallHue = 0.0f; // cycles 0..360

// --------------------------- GAME STATES -------------------------------------
enum GameState { STATE_MENU, STATE_PLAYING, STATE_WIN, STATE_LOSE };
GameState gameState = STATE_MENU;
bool pauseSim = false;

// --------------------------- VECTOR & CAMERA (Lab6 preserved) -----------------
#define DEG2RAD(a) (a * 0.0174532925f)

class Vector3f {
public:
    float x, y, z;
    Vector3f(float _x = 0.0f, float _y = 0.0f, float _z = 0.0f) { x = _x; y = _y; z = _z; }
    Vector3f operator+(Vector3f& v) { return Vector3f(x + v.x, y + v.y, z + v.z); }
    Vector3f operator-(Vector3f& v) { return Vector3f(x - v.x, y - v.y, z - v.z); }
    Vector3f operator*(float n) { return Vector3f(x * n, y * n, z * n); }
    Vector3f operator/(float n) { return Vector3f(x / n, y / n, z / n); }
    Vector3f unit() {
        float l = sqrt(x * x + y * y + z * z);
        if (l == 0.0f) return Vector3f(0, 0, 0);
        return Vector3f(x / l, y / l, z / l);
    }
    Vector3f cross(Vector3f v) { return Vector3f(y * v.z - z * v.y, z * v.x - x * v.z, x * v.y - y * v.x); }
};

class Camera {
public:
    Vector3f eye, center, up;
    Camera(float eyeX = 2.5f, float eyeY = 2.0f, float eyeZ = 3.0f,
        float centerX = 0.0f, float centerY = 0.5f, float centerZ = 0.0f,
        float upX = 0.0f, float upY = 1.0f, float upZ = 0.0f) {
        eye = Vector3f(eyeX, eyeY, eyeZ);
        center = Vector3f(centerX, centerY, centerZ);
        up = Vector3f(upX, upY, upZ);
    }
    void moveX(float d) {
        Vector3f right = up.cross(center - eye).unit();
        eye = eye + right * d; center = center + right * d;
    }
    void moveY(float d) { eye = eye + up.unit() * d; center = center + up.unit() * d; }
    void moveZ(float d) { Vector3f view = (center - eye).unit(); eye = eye + view * d; center = center + view * d; }
    void rotateX(float a) {
        Vector3f view = (center - eye).unit();
        Vector3f right = up.cross(view).unit();
        view = view * cos(DEG2RAD(a)) + up * sin(DEG2RAD(a));
        up = view.cross(right);
        center = eye + view;
    }
    void rotateY(float a) {
        Vector3f view = (center - eye).unit();
        Vector3f right = up.cross(view).unit();
        view = view * cos(DEG2RAD(a)) + right * sin(DEG2RAD(a));
        right = view.cross(up);
        center = eye + view;
    }
    void look() { gluLookAt(eye.x, eye.y, eye.z, center.x, center.y, center.z, up.x, up.y, up.z); }
};

Camera camera;

// --------------------------- SCENE BOUNDS ------------------------------------
const float BOUNDS_HALF_X = 10.0f;  // floor -10..+10
const float BOUNDS_HALF_Z = 10.0f;
const float FLOOR_Y = 0.0f;
const float CEILING_Y = 8.0f;

// --------------------------- HELPERS ----------------------------------------
float clampf(float v, float a, float b) { if (v < a) return a; if (v > b) return b; return v; }

// --------------------------- PLAYER -----------------------------------------
struct Player { Vector3f pos; Vector3f vel; float yaw; float pitch; bool onGround; float radius; } player;
void initPlayer() {
    player.pos = Vector3f(0.0f, FLOOR_Y + 0.8f, 0.0f); // center start
    player.vel = Vector3f(0, 0, 0);
    player.yaw = 0.0f; player.pitch = 0.0f;
    player.onGround = true; player.radius = 0.35f;
}

// --------------------------- GOAL -------------------------------------------
struct Goal { Vector3f pos; bool visible; float bobPhase; } goal;
void initGoal() {
    goal.pos = Vector3f(4.0f, FLOOR_Y + 0.6f, -3.0f);
    goal.visible = true; goal.bobPhase = 0.0f;
}

// --------------------------- OBJECTS & ANIMATION FLAGS -----------------------
bool animObj[6] = { false, false, false, false, false, false };
float animTime = 0.0f;

// --------------------------- INPUT STATE ------------------------------------
bool keysDown[256]; // default false

// --------------------------- TEXT & COLOR HELPERS ---------------------------
void drawText2D(float x, float y, const char* text) {
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    gluOrtho2D(0, windowWidth, 0, windowHeight);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glColor3f(1, 1, 1);
    glRasterPos2f(x, y);
    for (size_t i = 0; i < strlen(text); ++i) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_18, text[i]);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
    glEnable(GL_LIGHTING);
}
void hsvToRgb(float h, float s, float v, float& r, float& g, float& b) {
    if (s <= 0.0f) { r = g = b = v; return; }
    float hh = fmodf(h, 360.0f); hh /= 60.0f;
    int i = (int)hh; float ff = hh - i;
    float p = v * (1.0f - s), q = v * (1.0f - s * ff), t = v * (1.0f - s * (1.0f - ff));
    switch (i) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
}

// --------------------------- DRAW PRIMITIVES --------------------------------
void drawFloor() {
    glPushMatrix();
    glColor3f(0.12f, 0.12f, 0.18f);
    glTranslatef(0.0f, FLOOR_Y - 0.005f, 0.0f);
    glScalef(BOUNDS_HALF_X * 2.0f, 0.01f, BOUNDS_HALF_Z * 2.0f);
    glutSolidCube(1.0f);
    glPopMatrix();
}
void drawWallPanel(float x, float y, float z, float rotY = 0.0f) {
    // Panel + support column
    glPushMatrix(); glTranslatef(x, y + 1.0f, z); glRotatef(rotY, 0, 1, 0); glScalef(BOUNDS_HALF_X * 2.0f, 2.0f, 0.08f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glTranslatef(x - (BOUNDS_HALF_X * 2.0f - 0.25f) / 2.0f, y + 1.0f, z); glRotatef(rotY, 0, 1, 0); glScalef(0.25f, 2.0f, 0.25f); glutSolidCube(1.0f); glPopMatrix();
}

// Player model (7 primitives)
void drawPlayerModel() {
    glPushMatrix();
    glTranslatef(player.pos.x, player.pos.y, player.pos.z);
    glRotatef(player.yaw, 0, 1, 0); glRotatef(player.pitch, 1, 0, 0);

    // Torso
    glPushMatrix(); glColor3f(0.85f, 0.85f, 0.9f); glTranslatef(0.0f, 0.6f, 0.0f); glScalef(0.5f, 0.6f, 0.3f); glutSolidCube(1.0f); glPopMatrix();
    // Head
    glPushMatrix(); glColor3f(0.95f, 0.95f, 0.98f); glTranslatef(0.0f, 1.3f, 0.0f); glutSolidSphere(0.20f, 20, 20); glPopMatrix();
    // Left arm
    glPushMatrix(); glColor3f(0.85f, 0.85f, 0.9f); glTranslatef(-0.40f, 0.9f, 0.0f); glScalef(0.15f, 0.45f, 0.15f); glutSolidCube(1.0f); glPopMatrix();
    // Right arm
    glPushMatrix(); glColor3f(0.85f, 0.85f, 0.9f); glTranslatef(0.40f, 0.9f, 0.0f); glScalef(0.15f, 0.45f, 0.15f); glutSolidCube(1.0f); glPopMatrix();
    // Left leg
    glPushMatrix(); glColor3f(0.75f, 0.75f, 0.8f); glTranslatef(-0.18f, 0.35f, 0.0f); glScalef(0.16f, 0.50f, 0.16f); glutSolidCube(1.0f); glPopMatrix();
    // Right leg
    glPushMatrix(); glColor3f(0.75f, 0.75f, 0.8f); glTranslatef(0.18f, 0.35f, 0.0f); glScalef(0.16f, 0.50f, 0.16f); glutSolidCube(1.0f); glPopMatrix();
    // Backpack
    glPushMatrix(); glColor3f(0.65f, 0.65f, 0.7f); glTranslatef(0.0f, 0.95f, -0.22f); glScalef(0.25f, 0.35f, 0.10f); glutSolidCube(1.0f); glPopMatrix();

    glPopMatrix();
}

// Goal (3 primitives)
void drawGoal(bool bobbing = true) {
    if (!goal.visible) return;
    float bobY = bobbing ? 0.12f * sinf(goal.bobPhase) : 0.0f;
    glPushMatrix(); glTranslatef(goal.pos.x, goal.pos.y + bobY, goal.pos.z);
    // body (cube)
    glPushMatrix(); glColor3f(0.8f, 0.5f, 0.05f); glScalef(0.3f, 0.6f, 0.3f); glutSolidCube(1.0f); glPopMatrix();
    // core sphere
    glPushMatrix(); glColor3f(1.0f, 0.9f, 0.2f); glutSolidSphere(0.12f, 18, 18); glPopMatrix();
    // top cap
    glPushMatrix(); glColor3f(0.35f, 0.35f, 0.4f); glTranslatef(0.0f, 0.35f, 0.0f); glScalef(0.18f, 0.06f, 0.18f); glutSolidCube(1.0f); glPopMatrix();
    glPopMatrix();
}

// Solar, cargo, drone, airlock, control panel implementations (same as earlier)
void drawSolarArray(float worldX, float worldY, float worldZ, float hingeAngle) {
    glPushMatrix(); glTranslatef(worldX, worldY, worldZ);
    glPushMatrix(); glColor3f(0.3f, 0.3f, 0.35f); glScalef(0.4f, 0.2f, 0.4f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.22f, 0.0f); glRotatef(hingeAngle, 1, 0, 0); glColor3f(0.45f, 0.45f, 0.5f); glScalef(0.15f, 0.08f, 0.15f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.22f, -0.6f); glRotatef(hingeAngle, 1, 0, 0); glColor3f(0.25f, 0.25f, 0.28f); glScalef(0.1f, 0.05f, 1.2f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.22f, -1.05f); glRotatef(hingeAngle, 1, 0, 0); glColor3f(0.05f, 0.15f, 0.55f); glScalef(0.9f, 0.02f, 1.8f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glColor3f(0.4f, 0.4f, 0.45f); glTranslatef(0.6f, 0.0f, -0.6f); glScalef(0.06f, 0.06f, 0.6f); glutSolidCube(1.0f); glPopMatrix();
    glPopMatrix();
}
void drawCargoStack(float wx, float wy, float wz, float bobOffset) {
    glPushMatrix(); glTranslatef(wx, wy + bobOffset, wz);
    glPushMatrix(); glColor3f(0.4f, 0.25f, 0.12f); glScalef(1.2f, 0.08f, 1.2f); glTranslatef(0.0f, -0.4f, 0.0f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glColor3f(0.6f, 0.4f, 0.2f); glTranslatef(0.0f, -0.15f, 0.0f); glScalef(0.6f, 0.4f, 0.6f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glColor3f(0.6f, 0.42f, 0.22f); glTranslatef(0.0f, 0.22f, 0.0f); glScalef(0.55f, 0.35f, 0.55f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glColor3f(0.58f, 0.4f, 0.2f); glTranslatef(0.0f, 0.58f, 0.0f); glScalef(0.5f, 0.32f, 0.5f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glColor3f(0.35f, 0.35f, 0.38f); glTranslatef(0.45f, 0.0f, 0.45f); glScalef(0.05f, 0.6f, 0.05f); glutSolidCube(1.0f); glPopMatrix();
    glPopMatrix();
}
void drawRepairDrone(float wx, float wy, float wz, float spin) {
    glPushMatrix(); glTranslatef(wx, wy, wz); glRotatef(spin, 0, 1, 0);
    glPushMatrix(); glColor3f(0.8f, 0.2f, 0.2f); glutSolidSphere(0.18f, 18, 18); glPopMatrix();
    glPushMatrix(); glColor3f(0.45f, 0.45f, 0.5f); glTranslatef(-0.28f, 0.0f, 0.0f); glScalef(0.12f, 0.03f, 0.5f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glColor3f(0.45f, 0.45f, 0.5f); glTranslatef(0.28f, 0.0f, 0.0f); glScalef(0.12f, 0.03f, 0.5f); glutSolidCube(1.0f); glPopMatrix();
    glPopMatrix();
}
void drawAirlockGate(float wx, float wy, float wz, float openScale) {
    glPushMatrix(); glTranslatef(wx, wy, wz);
    glPushMatrix(); glColor3f(0.4f, 0.4f, 0.46f); glScalef(0.6f, 1.2f, 0.12f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glTranslatef(0.0f, 0.0f, -0.06f); glColor3f(0.7f, 0.7f, 0.75f); glutSolidTorus(0.03, 0.18, 12, 30); glPopMatrix();
    glPushMatrix(); glColor3f(0.9f, 0.9f, 0.95f); glTranslatef(0.0f, 0.0f, 0.01f); glScalef(openScale, 0.9f, 0.05f); glutSolidCube(1.0f); glPopMatrix();
    glPopMatrix();
}
void drawControlPanel(float wx, float wy, float wz, float pulse) {
    glPushMatrix(); glTranslatef(wx, wy, wz);
    glPushMatrix(); glColor3f(0.2f, 0.2f, 0.25f); glScalef(0.6f, 0.3f, 0.3f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glColor3f(0.05f, 0.15f, 0.55f + 0.05f * pulse); glTranslatef(0.0f, 0.2f, -0.15f); glScalef(0.45f, 0.25f, 0.02f); glutSolidCube(1.0f); glPopMatrix();
    glPushMatrix(); glColor3f(0.3f, 0.3f, 0.33f); glTranslatef(0.0f, 0.05f, 0.12f); glScalef(0.4f, 0.05f, 0.12f); glutSolidCube(1.0f); glPopMatrix();
    glPopMatrix();
}

// --------------------------- LIGHTING ---------------------------------------
void setupLights() {
    GLfloat mat_ambient[] = { 0.7f, 0.7f, 0.7f, 1.0f };
    GLfloat mat_diffuse[] = { 0.6f, 0.6f, 0.6f, 1.0f };
    GLfloat mat_specular[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    GLfloat mat_shininess[] = { 50.0f };
    glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, mat_ambient);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, mat_diffuse);
    glMaterialfv(GL_FRONT, GL_SPECULAR, mat_specular);
    glMaterialfv(GL_FRONT, GL_SHININESS, mat_shininess);
    GLfloat lightIntensity[] = { 0.8f, 0.8f, 0.9f, 1.0f };
    GLfloat lightPosition[] = { -7.0f, 8.0f, 3.0f, 0.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightPosition);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightIntensity);
}

// --------------------------- CAMERA / PROJECTION -----------------------------
void setupCamera() {
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    gluPerspective(60.0f, (double)windowWidth / (double)windowHeight, 0.01, 200.0);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity(); camera.look();
}

// --------------------------- COLLISION & PHYSICS ----------------------------
void clampPlayerToBounds() {
    float minX = -BOUNDS_HALF_X + player.radius;
    float maxX = BOUNDS_HALF_X - player.radius;
    float minZ = -BOUNDS_HALF_Z + player.radius;
    float maxZ = BOUNDS_HALF_Z - player.radius;
    float minY = FLOOR_Y + player.radius;
    float maxY = CEILING_Y - player.radius;
    player.pos.x = clampf(player.pos.x, minX, maxX);
    player.pos.z = clampf(player.pos.z, minZ, maxZ);
    player.pos.y = clampf(player.pos.y, minY, maxY);
}
bool checkPlayerGoalCollision() {
    if (!goal.visible) return false;
    float dx = player.pos.x - goal.pos.x;
    float dy = player.pos.y - goal.pos.y;
    float dz = player.pos.z - goal.pos.z;
    float distSq = dx * dx + dy * dy + dz * dz;
    float thresh = (player.radius + 0.4f);
    return distSq <= thresh * thresh;
}

// --------------------------- INPUT & MOVEMENT -------------------------------
void applyPlayerInput(float dt) {
    Vector3f dir(0, 0, 0);
    if (keysDown['w'] || keysDown['W']) dir.z += -1.0f;
    if (keysDown['s'] || keysDown['S']) dir.z += +1.0f;
    if (keysDown['a'] || keysDown['A']) dir.x += -1.0f;
    if (keysDown['d'] || keysDown['D']) dir.x += +1.0f;
    if (keysDown[' ']) dir.y += +1.0f; // space up
    if (keysDown['q'] || keysDown['Q']) dir.y += +1.0f; // q up
    if (keysDown['e'] || keysDown['E']) dir.y -= 1.0f;  // e down

    float lenXZ = sqrtf(dir.x * dir.x + dir.z * dir.z);
    Vector3f movement(0, 0, 0);
    if (lenXZ > 0.0f) { movement.x = (dir.x / lenXZ) * PLAYER_SPEED * dt; movement.z = (dir.z / lenXZ) * PLAYER_SPEED * dt; }
    if (dir.y > 0.0f) movement.y = 2.0f * PLAYER_SPEED * dt;
    if (dir.y < 0.0f) movement.y = -2.0f * PLAYER_SPEED * dt;

    player.pos.x += movement.x;
    player.pos.y += movement.y;
    player.pos.z += movement.z;
    clampPlayerToBounds();

    float epsilon = 0.01f;
    player.onGround = (player.pos.y <= FLOOR_Y + 0.8f + epsilon);
    if (player.onGround) {
        if (lenXZ > 0.01f) {
            float ang = atan2f(movement.x, -movement.z) * 180.0f / 3.14159265f;
            player.yaw = ang; player.pitch = 0.0f;
        }
    }
    else {
        player.pitch = -AIR_TILT_DEG;
        if (lenXZ > 0.01f) {
            float ang = atan2f(movement.x, -movement.z) * 180.0f / 3.14159265f;
            player.yaw = ang;
        }
    }
}

void keyDown(unsigned char key, int x, int y) {
    keysDown[key] = true;

    // ENTER key starts or restarts
    if (key == 13) { // ENTER
        if (gameState == STATE_MENU) {
            playSoundEffect("hit.wav");
            initPlayer(); initGoal(); for (int i = 0; i < 6; i++) animObj[i] = false;
            gameStartMillis = glutGet(GLUT_ELAPSED_TIME);
            gameState = STATE_PLAYING;

            playBackgroundMusic();
            pauseSim = false;
        }
        else if (gameState == STATE_WIN || gameState == STATE_LOSE) {
            // return to menu
            stopBackgroundMusic();
            gameState = STATE_MENU;

        }
        return;
    }

    if (gameState != STATE_PLAYING) {
        // In menu or end screens, other keys ignored
        return;
    }

    switch (key) {
    case 27: exit(0); break;
    case 'p': case 'P': pauseSim = !pauseSim; break;
    case '1':
        camera.eye = Vector3f(0.0f, 3.0f, 12.0f); camera.center = Vector3f(0.0f, 1.0f, 0.0f); camera.up = Vector3f(0, 1, 0); break;
    case '2':
        camera.eye = Vector3f(12.0f, 3.0f, 0.0f); camera.center = Vector3f(0.0f, 1.0f, 0.0f); camera.up = Vector3f(0, 1, 0); break;
    case '3':
        camera.eye = Vector3f(0.0f, 18.0f, 0.01f); camera.center = Vector3f(0.0f, 0.0f, 0.0f); camera.up = Vector3f(0, 0, -1); break;

        // animation toggles
    case 'z': case 'Z': animObj[1] = true; break;
    case 'x': case 'X': animObj[1] = false; break;
    case 'c': case 'C': animObj[2] = true; break;
    case 'v': case 'V': animObj[2] = false; break;
    case 'b': case 'B': animObj[3] = true; break;
    case 'n': case 'N': animObj[3] = false; break;
    case 'm': case 'M': animObj[4] = true; break;
    case ',':           animObj[4] = false; break;
    case '.':           animObj[5] = true; break;
    case '/':           animObj[5] = false; break;

        // camera movement helpers
    case 'i': case 'I': camera.moveZ(-0.2f); break;
    case 'k': case 'K': camera.moveZ(0.2f); break;
    case 'j': case 'J': camera.moveX(-0.2f); break;
    case 'l': case 'L': camera.moveX(0.2f); break;
    case 'u': case 'U': camera.moveY(0.2f); break;
    case 'o': case 'O': camera.moveY(-0.2f); break;

    case 'r': case 'R':
        initPlayer(); initGoal(); for (int i = 0; i < 6; i++) animObj[i] = false;
        gameStartMillis = glutGet(GLUT_ELAPSED_TIME);
        pauseSim = false;
        break;
    }
}

void keyUp(unsigned char key, int x, int y) {
    keysDown[key] = false;
}

// Special keys for camera rotation
void specialKeyDown(int key, int x, int y) {
    float a = 2.0f;
    switch (key) {
    case GLUT_KEY_UP:    camera.rotateX(a); break;
    case GLUT_KEY_DOWN:  camera.rotateX(-a); break;
    case GLUT_KEY_LEFT:  camera.rotateY(a); break;
    case GLUT_KEY_RIGHT: camera.rotateY(-a); break;
    }
}

// --------------------------- RENDERING -------------------------------------
void renderScene() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (gameState == STATE_MENU) {
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        gluOrtho2D(0, windowWidth, 0, windowHeight);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        glDisable(GL_LIGHTING);

        drawText2D(200, windowHeight - 140, "SPACE STATION — MISSION BRIEFING");
        drawText2D(140, windowHeight - 190, "Objective: Locate and collect the glowing Power Cell floating in the station.");
        drawText2D(140, windowHeight - 220, "Move: W/A/S/D — Up/Down: SPACE / Q / E — Camera: IJKLUO — Views: 1/2/3");
        drawText2D(140, windowHeight - 250, "Animations: Z/X (Solar Array), C/V (Cargo), B/N (Drone), M/, (Airlock), .// (Control Panel)");
        drawText2D(140, windowHeight - 290, "Collect the Power Cell BEFORE the 120-second timer ends...");
        drawText2D(140, windowHeight - 330, "Press ENTER to begin mission.");

        glEnable(GL_LIGHTING);

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        glutSwapBuffers();
        return;
    }

    // ------------------ PLAYING / WIN / LOSE 3D SCENE ------------------
    setupCamera();
    setupLights();

    // Wall color cycling
    wallHue += 20.0f * (1.0f / 60.0f); if (wallHue >= 360.0f) wallHue -= 360.0f;
    float wr, wg, wb; hsvToRgb(fmodf(wallHue, 360.0f), 0.45f, 0.85f, wr, wg, wb);

    // Draw floor and walls
    drawFloor();
    glColor3f(wr, wg, wb);
    drawWallPanel(0.0f, FLOOR_Y, -BOUNDS_HALF_Z, 0.0f);
    drawWallPanel(0.0f, FLOOR_Y, BOUNDS_HALF_Z, 180.0f);
    drawWallPanel(-BOUNDS_HALF_X, FLOOR_Y, 0.0f, 90.0f);
    drawWallPanel(BOUNDS_HALF_X, FLOOR_Y, 0.0f, -90.0f);

    // Draw animated objects
    float solarHinge = animObj[1] ? 30.0f * sinf(animTime * 0.6f) : 0.0f;
    drawSolarArray(-6.0f, FLOOR_Y + 0.8f, -5.0f, solarHinge);

    float cargoBob = animObj[2] ? 0.15f * sinf(animTime * 1.6f) : 0.0f;
    drawCargoStack(6.0f, FLOOR_Y + 0.6f, -5.0f, cargoBob);

    float droneSpin = animObj[3] ? fmodf(animTime * 90.0f, 360.0f) : 0.0f;
    drawRepairDrone(0.0f, FLOOR_Y + 1.6f, -4.0f, droneSpin);

    float airlockScale = animObj[4] ? 1.0f + 0.5f * sinf(animTime * 2.0f) : 1.0f;
    drawAirlockGate(0.0f, FLOOR_Y + 0.9f, 6.0f, airlockScale);

    float controlPulse = animObj[5] ? 0.5f * (0.5f + 0.5f * sinf(animTime * 4.0f)) : 0.0f;
    drawControlPanel(-5.0f, FLOOR_Y + 0.6f, 4.0f, controlPulse);

    // Draw goal and player
    drawGoal(true);
    drawPlayerModel();

    // ------------------ HUD ------------------
    if (gameState == STATE_PLAYING) {
        int elapsed = glutGet(GLUT_ELAPSED_TIME) - gameStartMillis;
        int remain = gameDurationMillis - elapsed; if (remain < 0) remain = 0;
        char buf[64]; sprintf(buf, "Time remaining: %d s", remain / 1000); drawText2D(10, windowHeight - 24, buf);
    }
    else {
        drawText2D(10, windowHeight - 24, "Time remaining: --");
    }
    char buf2[64]; sprintf(buf2, "Goals left: %d", goal.visible ? 1 : 0); drawText2D(10, windowHeight - 48, buf2);
    drawText2D(10, 10, "Press 1/2/3 for views. ENTER to (re)start. P pause. R reset.");

    // ------------------ WIN / LOSE OVERLAY ------------------
    if (gameState == STATE_WIN || gameState == STATE_LOSE) {
        stopBackgroundMusic();
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        gluOrtho2D(0, windowWidth, 0, windowHeight);

        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        // Semi-transparent dark overlay
        glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
        glBegin(GL_QUADS);
        glVertex2f(0, 0);
        glVertex2f(windowWidth, 0);
        glVertex2f(windowWidth, windowHeight);
        glVertex2f(0, windowHeight);
        glEnd();

        const char* msg = (gameState == STATE_WIN)
            ? "MISSION COMPLETE — POWER CELL RECOVERED!"
            : "MISSION FAILED — TIME RAN OUT";

        if (gameState == STATE_WIN) {
            playSoundEffect("win.wav");
        }
        else {
            playSoundEffect("lose.wav");
        }

        drawText2D(windowWidth / 2 - 180, windowHeight / 2, msg);
        drawText2D(windowWidth / 2 - 120, windowHeight / 2 - 40, "Press ENTER to return to Mission Briefing");

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);

        glEnable(GL_LIGHTING);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }

    glutSwapBuffers();
}


// --------------------------- UPDATE (TIMER) ---------------------------------
void updateScene(int value) {
    // call frequently
    glutTimerFunc(16, updateScene, 0);

    if (gameState != STATE_PLAYING) {
        // update animations' internal phases so menu anims (if any) still look alive (optional)
        return;
    }
    if (pauseSim) return;

    float dt = 0.016f; animTime += dt; goal.bobPhase += dt * 2.0f;

    // Input-driven movement
    applyPlayerInput(dt);

    // Check goal collision and timer
    if (checkPlayerGoalCollision()) {
        goal.visible = false;
        playSoundEffect("collect.wav");
        int elapsed = glutGet(GLUT_ELAPSED_TIME) - gameStartMillis;
        if (elapsed <= gameDurationMillis) { gameState = STATE_WIN; }
    }

    int elapsed = glutGet(GLUT_ELAPSED_TIME) - gameStartMillis;
    if (elapsed >= gameDurationMillis) {
        if (goal.visible) {
            gameState = STATE_LOSE;
            playSoundEffect("lose.wav");
        }
        else {
            gameState = STATE_WIN;
            playSoundEffect("win.wav");
        }
    }

    glutPostRedisplay();
}

// --------------------------- INITIALIZATION ---------------------------------
void onResize(int w, int h) { if (h == 0) h = 1; windowWidth = w; windowHeight = h; glViewport(0, 0, w, h); }

void initAll() {
    // initialize inputs
    for (int i = 0; i < 256; i++) keysDown[i] = false;
    // initial player/goal
    initPlayer(); initGoal();
    animTime = 0.0f; for (int i = 0; i < 6; i++) animObj[i] = false;
    // GL states
    glEnable(GL_DEPTH_TEST); glEnable(GL_LIGHTING); glEnable(GL_LIGHT0); glEnable(GL_NORMALIZE); glEnable(GL_COLOR_MATERIAL);
    glShadeModel(GL_SMOOTH); glClearColor(0.02f, 0.02f, 0.04f, 1.0f);
    // Start camera in a good default that shows the scene
    camera.eye = Vector3f(0.0f, 4.0f, 14.0f); camera.center = Vector3f(0.0f, 1.0f, 0.0f); camera.up = Vector3f(0, 1, 0);
}

// --------------------------- MAIN -------------------------------------------
int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(windowWidth, windowHeight);
    glutInitWindowPosition(100, 50);
    glutCreateWindow("P15-58-0352 - Space Station (Assignment 2)");

    initAll();

    glutDisplayFunc(renderScene);
    glutReshapeFunc(onResize);
    glutKeyboardFunc(keyDown);
    glutKeyboardUpFunc(keyUp);
    glutSpecialFunc(specialKeyDown);

    glutTimerFunc(16, updateScene, 0);
    glutMainLoop();
    return 0;
}
