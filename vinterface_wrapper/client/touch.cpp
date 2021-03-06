/*
touch.cpp -- touchcontrols
Copyright (C) 2017 a1batross
Licensed under WTFPL license.
Thanks to Valve for SDK, me for idea.
Please, don't punish, Mr. Newell. :)
*/

#include "client.h"
#include "convar.h"
#include <dlfcn.h>
#include <string.h>
#include "wrapper.h"
#include "vgui/IInputInternal.h"
#include <jni.h>
#include "GLES2/gl2.h"
#include "MenuTouch.h"

#define boundmax( num, high ) ( (num) < (high) ? (num) : (high) )
#define boundmin( num, low )  ( (num) >= (low) ? (num) : (low)  )
#define bound( low, num, high ) ( boundmin( boundmax(num, high), low ))
#define S

#define TOUCH_YAW 120
#define TOUCH_PITCH 90
#define TOUCH_FORWARDZONE 0.8
#define TOUCH_SIDEZONE 0.12
#define SENSITIVITY 2

CTouchControls g_Touch;
static CTouchButton g_DefaultButtons[64];
static int g_LastDefaultButton = 0;
int screen_h, screen_w;

static CTouchButton g_Buttons[64];
static int g_LastButton = 0;

void *mainHandle;

typedef signed int (*t_onNativeJoystickAxis)(JNIEnv *env, jclass *clazz, jint n, jfloat pitch);
t_onNativeJoystickAxis onNativeJoystickAxis;

CTouchControls::CTouchControls()
{
    initialized = false;
}

void *gl4es;
t_showKeyboard showKeyboard;

CTouchControls::~CTouchControls()
{
}

typedef void (*t_glEnable)(GLenum cap);
t_glEnable _glEnable;

static bool show_touch = true;

DLLEXPORT void showTouch(bool show)
{
	LogPrintf("showTouch called\n");
	show_touch = show;
}

#define GRID_COUNT 50
#define GRID_COUNT_X (GRID_COUNT)
#define GRID_COUNT_Y (GRID_COUNT * screen_h / screen_w)
#define GRID_X (1.0/GRID_COUNT_X)
#define GRID_Y (screen_w/screen_h/GRID_COUNT_X)
#define GRID_ROUND_X(x) ((float)round( x * GRID_COUNT_X ) / GRID_COUNT_X)
#define GRID_ROUND_Y(x) ((float)round( x * GRID_COUNT_Y ) / GRID_COUNT_Y)

static void IN_TouchCheckCoords( float *x1, float *y1, float *x2, float *y2  )
{
	/// TODO: grid check here
	if( *x2 - *x1 < GRID_X * 2 )
		*x2 = *x1 + GRID_X * 2;
	if( *y2 - *y1 < GRID_Y * 2)
		*y2 = *y1 + GRID_Y * 2;
	if( *x1 < 0 )
		*x2 -= *x1, *x1 = 0;
	if( *y1 < 0 )
		*y2 -= *y1, *y1 = 0;
	if( *y2 > 1 )
		*y1 -= *y2 - 1, *y2 = 1;
	if( *x2 > 1 )
		*x1 -= *x2 - 1, *x2 = 1;
	
	*x1 = GRID_ROUND_X( *x1 );
	*x2 = GRID_ROUND_X( *x2 );
	*y1 = GRID_ROUND_Y( *y1 );
	*y2 = GRID_ROUND_Y( *y2 );
}


void CTouchControls::Init( )
{
    if( initialized )
        return;

    engine->GetScreenSize( screen_w, screen_h );
    void *mainHandle = dlopen("libmain.so",0);
    void (*clientLoaded)(void) = (void (*)(void))dlsym( mainHandle, "clientLoaded" );
    showKeyboard = (t_showKeyboard)dlsym( mainHandle, "showKeyboard" );
    clientLoaded();

    void *sdl2 = dlopen("libSDL2.so",0);
    void (*SDL_StartTextInput)(void) = (void (*)(void))dlsym(sdl2, "SDL_StartTextInput" );
    onNativeJoystickAxis = (t_onNativeJoystickAxis)dlsym(sdl2, "Java_org_libsdl_app_SDLActivity_onNativeJoystickAxis");
        SDL_StartTextInput();
        dlclose( sdl2 );

    w = a = s = d = false;
    initialized = true;
    btns.EnsureCapacity( 64 );
    vhandle = 0;
    look_finger = move_finger = resize_finger = -1;
    forward = side = movecount = 0;
    scolor = rgba_t( -1, -1, -1, -1 );
    state = state_none;
    swidth = 1;
    move = edit = selection = NULL;
    showbuttons = true;
    clientonly = false;
    precision = false;
    mouse_events = 0;

    overlayPanel = new COverlayPanel(NULL, "OverlayPanel");
    overlayPanel->SetParent(enginevgui->GetPanel(PANEL_GAMEUIDLL));
    overlayPanel->SetVisible(true);
    overlayPanel->MoveToFront();

    move_start_x = move_start_y = 0.0f;

    // textures
    showtexture = hidetexture = resettexture = closetexture = joytexture = 0;
    configchanged = false;

    rgba_t color(255, 255, 255, 255);

    // buttons
    bool is_portal = strstr(getenv("LIBRARY_CLIENT"), "portal" );
    IN_TouchAddDefaultButton( "console", "vgui/touch/showconsole", "showconsole", KEY_NONE, touch_command, 0.920000, 0, 1.000000, 0.1422222, color, 0, 0 );
    IN_TouchAddButton( "use", "vgui/touch/use", "+use", touch_command, 0.880000, 0.213333, 1.000000, 0.426667, -1, color );
    IN_TouchAddButton( "jump", "vgui/touch/jump", "+jump", touch_command, 0.880000, 0.462222, 1.000000, 0.675556, -1, color );
    IN_TouchAddButton( "attack", "vgui/touch/shoot", "+attack", touch_command, 0.760000, 0.583333, 0.880000, 0.796667, -1, color );
    IN_TouchAddButton( "attack2", "vgui/touch/shoot_alt", "+attack2", touch_command, 0.760000, 0.320000, 0.880000, 0.533333, -1, color );
    IN_TouchAddButton( "duck", "vgui/touch/crouch", "+duck", touch_command, 0.880000, 0.746667, 1.000000, 0.960000, -1, color );
    IN_TouchAddButton( "tduck", "vgui/touch/tduck", ";+duck", touch_command, 0.560000, 0.817778, 0.620000, 0.924444, -1, color );
    IN_TouchAddButton( "look", "", "", touch_look, 0.5, 0, 1, 1, -1, color );
    IN_TouchAddButton( "move", "", "", touch_move, 0, 0, 0.5, 1, -1, color );
    if( !is_portal )
    {
        IN_TouchAddButton( "zoom", "vgui/touch/zoom", "+zoom", touch_command, 0.680000, 0.00000, 0.760000, 0.142222, -1, color );
        IN_TouchAddButton( "speed", "vgui/touch/speed", "+speed", touch_command, 0.180000, 0.568889, 0.280000, 0.746667, -1, color );
        IN_TouchAddButton( "loadquick", "vgui/touch/load", "load quick", touch_command, 0.760000, 0.000000, 0.840000, 0.142222, -1, color );
        IN_TouchAddButton( "savequick", "vgui/touch/save", "save quick", touch_command, 0.840000, 0.000000, 0.920000, 0.142222, -1, color );
        IN_TouchAddButton( "reload", "vgui/touch/reload", "+reload", touch_command, 0.000000, 0.320000, 0.120000, 0.533333, -1, color );
        IN_TouchAddButton( "flashlight", "vgui/touch/flash_light_filled", "impulse 100", touch_command, 0.920000, 0.000000, 1.000000, 0.142222, -1, color );
        IN_TouchAddButton( "invnext", "vgui/touch/next_weap", "invnext", touch_command, 0.000000, 0.533333, 0.120000, 0.746667, -1, color );
        IN_TouchAddButton( "invprev", "vgui/touch/prev_weap", "invprev", touch_command, 0.000000, 0.071111, 0.120000, 0.284444, -1, color );
    }
    else
    {
        IN_TouchAddButton( "loadquick", "vgui/touch/load", "load quick", touch_command, 0.840000, 0.000000, 0.920000, 0.142222, -1, color );
        IN_TouchAddButton( "savequick", "vgui/touch/save", "save quick", touch_command, 0.920000, 0.000000, 1.000000, 0.142222, -1, color );
    }

    IN_TouchAddButton( "menu", "vgui/touch/menu", "gameui_activate", touch_command, 0.000000, 0.00000, 0.080000, 0.142222, -1, color );

    Msg( "CTouchControls::Init()" );
}

void CTouchControls::VidInit( )
{

}

void CTouchControls::Shutdown( )
{
}

void CTouchControls::IN_Move()
{
    if( movecount )
    {
        onNativeJoystickAxis( NULL, NULL, 0, -(side / movecount) ); // x axis
        onNativeJoystickAxis( NULL, NULL, 1, -(forward / movecount) ); // y axis

        movecount = side = forward = 0;
    }

/*
    if( side > 0.9 && !a )
    {
        a = true;
        engine->ClientCmd("+moveleft");
    }
    else if( a )
    {
        a = false;
        engine->ClientCmd("-moveleft");
    }

    if( side < -0.9 && !d )
    {
        d = true;
        engine->ClientCmd("+moveright");
    }
    else if( d )
    {
        d = false;
        engine->ClientCmd("-moveright");
    }

    if( forward < -0.7 && !s )
    {
        s = true;
        engine->ClientCmd("+back");
    }
    else if( s )
    {
        s = false;
        engine->ClientCmd("-back");
    }

    if( forward > 0.7 && !w )
    {
        w = true;
        engine->ClientCmd("+forward");
    }
    else if( w )
    {
        w = false;
        engine->ClientCmd("-forward");
    }
*/
}

void CTouchControls::IN_Look()
{
    if( !pitch && !yaw )
        return;
	
    QAngle ang;
    engine->GetViewAngles( ang );
    ang.x += pitch;
    ang.y += yaw;
    engine->SetViewAngles( ang );
    pitch = yaw = 0;
}

void CTouchControls::Frame()
{
    if (!initialized)
        return;

    IN_Move();
    IN_Look();
}

void CTouchControls::Paint( )
{
    if (!initialized)
        return;

    if ( enginevgui->IsGameUIVisible() )
    {
        for (int i = 0; i < g_LastDefaultButton; i++)
        {
            g_pSurface->DrawSetColor(255, 255, 255, 155);
            g_pSurface->DrawSetTexture( g_DefaultButtons[i].textureID );
            g_pSurface->DrawTexturedRect( g_DefaultButtons[i].x1*screen_w, g_DefaultButtons[i].y1*screen_h, g_DefaultButtons[i].x2*screen_w, g_DefaultButtons[i].y2*screen_h );
        }
    }
    else
    {
	if( !show_touch )
		return;
        for (int i = 0; i < g_LastButton; i++)
        {
            if( g_Buttons[i].type == touch_move || g_Buttons[i].type == touch_look )
                continue;

            g_pSurface->DrawSetColor(255, 255, 255, 155);
            g_pSurface->DrawSetTexture( g_Buttons[i].textureID );
            g_pSurface->DrawTexturedRect( g_Buttons[i].x1*screen_w, g_Buttons[i].y1*screen_h, g_Buttons[i].x2*screen_w, g_Buttons[i].y2*screen_h );
        }
    }
}

void CTouchControls::IN_TouchAddDefaultButton( const char *name, const char *texturefile, const char *command, vgui::KeyCode key, ETouchButtonType type, float x1, float y1, float x2, float y2, rgba_t color, float aspect, int flags )
{
    if( g_LastDefaultButton >= 64 )
        return;

    strncpy( g_DefaultButtons[g_LastDefaultButton].name, name, 32 );
    strncpy( g_DefaultButtons[g_LastDefaultButton].texturefile, texturefile, 256 );
    strncpy( g_DefaultButtons[g_LastDefaultButton].command, command, 256 );
    g_DefaultButtons[g_LastDefaultButton].key = key;
    g_DefaultButtons[g_LastDefaultButton].x1 =  x1;
    g_DefaultButtons[g_LastDefaultButton].y1 =  y1 ;
    g_DefaultButtons[g_LastDefaultButton].x2 =  x2;
    g_DefaultButtons[g_LastDefaultButton].y2 = y2;
    g_DefaultButtons[g_LastDefaultButton].color = color;
    g_DefaultButtons[g_LastDefaultButton].type = type;
    g_DefaultButtons[g_LastDefaultButton].aspect = aspect;
    g_DefaultButtons[g_LastDefaultButton].flags = flags;
    g_Buttons[g_LastButton].finger = -1;
    g_DefaultButtons[g_LastDefaultButton].textureID = g_pSurface->CreateNewTextureID();
    g_pSurface->DrawSetTextureFile( g_DefaultButtons[g_LastDefaultButton].textureID, g_DefaultButtons[g_LastDefaultButton].texturefile, true, false);

    g_LastDefaultButton++;
}

void CTouchControls::IN_TouchAddButton( const char *name, const char *texturefile, const char *command, ETouchButtonType type, float x1, float y1, float x2, float y2, int fingerid, rgba_t color )
{
    if( g_LastButton >= 64 )
        return;

    strncpy( g_Buttons[g_LastButton].name, name, 32 );
    strncpy( g_Buttons[g_LastButton].texturefile, texturefile, 256 );
    strncpy( g_Buttons[g_LastButton].command, command, 256 );
	IN_TouchCheckCoords(&x1, &y1, &x2, &y2);	
    g_Buttons[g_LastButton].x1 = x1;
    g_Buttons[g_LastButton].y1 = y1;
    g_Buttons[g_LastButton].x2 = x2;
    g_Buttons[g_LastButton].y2 = y1 + ( x2 - x1 ) * (((float)screen_w)/screen_h);
	IN_TouchCheckCoords(&g_Buttons[g_LastButton].x1, &g_Buttons[g_LastButton].y1, &g_Buttons[g_LastButton].x2, &g_Buttons[g_LastButton].y2);
    g_Buttons[g_LastButton].color = color;
    g_Buttons[g_LastButton].type = type;
    g_Buttons[g_LastButton].finger = -1;
    g_Buttons[g_LastButton].textureID = g_pSurface->CreateNewTextureID();
    g_pSurface->DrawSetTextureFile( g_Buttons[g_LastButton].textureID, g_Buttons[g_LastButton].texturefile, true, false);

    g_LastButton++;
}

void CTouchControls::TouchMotion( event_t *ev )
{
    if( enginevgui->IsGameUIVisible() )
        return;

    float f, s;

    for (int i = 0; i < g_LastButton; i++)
    {
        if( g_Buttons[i].finger == ev->fingerid )
        {
            if( g_Buttons[i].type == touch_move )
            {
                //LogPrintf( "TouchMotion, touch_move x:%f, y:%f, startx:%f, starty:%f", x, y, move_start_x, move_start_y );
                f = ( move_start_y - ev->y ) / TOUCH_SIDEZONE;
                s = ( move_start_x - ev->x ) / TOUCH_SIDEZONE;
                forward += bound( -1, f, 1 );
                side += bound( -1, s, 1 );
                movecount++;
            }
            else if( g_Buttons[i].type == touch_look )
            {
                yaw += TOUCH_YAW * ( dx - ev->x ) * SENSITIVITY;
                pitch -= TOUCH_PITCH * ( dy - ev->y ) * SENSITIVITY;
                dx = ev->x;
                dy = ev->y;
            }
        }
    }
}

void CTouchControls::ButtonPress( event_t *ev )
{
    if( ev->type == event_touchdown )
    {
        if( enginevgui->IsGameUIVisible() )
        { //buttons in menu
            for (int i = 0; i < g_LastDefaultButton; i++)
            {
                if(  ev->x > g_DefaultButtons[i].x1 && ev->x < g_DefaultButtons[i].x2 && ev->y > g_DefaultButtons[i].y1 && ev->y < g_DefaultButtons[i].y2 )
                {
                    g_DefaultButtons[i].finger = ev->fingerid;
                    if( g_DefaultButtons[i].type == touch_key )
                        g_pInputInternal->InternalKeyCodePressed(g_DefaultButtons[i].key);
                    else if( g_DefaultButtons[i].type == touch_command )
                    {
                        engine->ClientCmd( g_DefaultButtons[i].command );
                    }
                }
            }
            g_pInputInternal->SetCursorPos((int)(ev->x*screen_w), (int)(ev->y*screen_h));
            g_pInputInternal->UpdateCursorPosInternal((int)(ev->x*screen_w), (int)(ev->y*screen_h));
            vgui::Panel *panel = vgui::ipanel()->GetPanel(g_pInputInternal->GetMouseOver(), "GameUI");
            MenuTouch::TouchDown(panel);
        }
        else
        {
            for (int i = 0; i < g_LastButton; i++)
            {
                if(  ev->x > g_Buttons[i].x1 && ev->x < g_Buttons[i].x2 && ev->y > g_Buttons[i].y1 && ev->y < g_Buttons[i].y2 )
                {
                    g_Buttons[i].finger = ev->fingerid;
                    if( g_Buttons[i].type == touch_move  )
                    {
                        if( move_finger == -1 )
                        {
                            move_start_x = ev->x;
                            move_start_y = ev->y;
                            move_finger = ev->fingerid;
                        }
                        else
                            g_Buttons[i].finger = move_finger;
                    }
                    else if( g_Buttons[i].type == touch_look )
                    {
                        if( look_finger == -1 )
                        {
                            dx = ev->x;
                            dy = ev->y;
                            look_finger = ev->fingerid;
                        }
                        else
                            g_Buttons[i].finger = look_finger;
                    }
                    else
                    {
						LogPrintf("Touch ClientCmd: %s\n", g_Buttons[i].command);						
                        engine->ClientCmd( g_Buttons[i].command );
                    }
                }
            }
        }
    }
    else if( ev->type == event_touchup )
    {
        if( enginevgui->IsGameUIVisible() )
        { //buttons in menu
            for (int i = 0; i < g_LastDefaultButton; i++)
            {
                if( g_DefaultButtons[i].finger == ev->fingerid )
                {
                    g_DefaultButtons[i].finger = -1;
                    if( g_DefaultButtons[i].type == touch_key )
                        g_pInputInternal->InternalKeyCodeReleased(g_DefaultButtons[i].key);
                }
            }
            g_pInputInternal->SetCursorPos((int)(ev->x*screen_w), (int)(ev->y*screen_h));
            g_pInputInternal->UpdateCursorPosInternal((int)(ev->x*screen_w), (int)(ev->y*screen_h));
            vgui::Panel *panel = vgui::ipanel()->GetPanel(g_pInputInternal->GetMouseOver(), "GameUI");
            MenuTouch::TouchUp(panel);
            g_pInputInternal->SetCursorPos((int)(screen_w/2), (int)(screen_h/2));
            g_pInputInternal->UpdateCursorPosInternal((int)(screen_w/2), (int)(screen_h/2));
        }
        else
        {
            for (int i = 0; i < g_LastButton; i++)
            {
                if( g_Buttons[i].finger == ev->fingerid )
                {
                    g_Buttons[i].finger = -1;

                    if( g_Buttons[i].type == touch_move )
                    {
                        forward = side = movecount = 0;
                        onNativeJoystickAxis( NULL, NULL, 0, 0 ); // x axis
                        onNativeJoystickAxis( NULL, NULL, 1, 0 ); // y axis

                        move_finger = -1;
                    }
                    else if( g_Buttons[i].type == touch_look )
                        look_finger = -1;
                    else if( g_Buttons[i].command[0] == '+' )
                    {
                        char cmd[256];
                        snprintf( cmd, sizeof cmd, "%s", g_Buttons[i].command );
                        cmd[0] = '-';
                        engine->ClientCmd( cmd );
                    }
                }
            }
        }
    }
}
