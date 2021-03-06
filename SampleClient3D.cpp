//=============================================================================
// Copyright ?2009 NaturalPoint, Inc. All Rights Reserved.
// 
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall NaturalPoint, Inc. or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//=============================================================================


// NatNetSample.cpp : Defines the entry point for the application.
//
#ifdef WIN32
#  define _CRT_SECURE_NO_WARNINGS
#  define WIN32_LEAN_AND_MEAN // Exclude rarely-used stuff from Windows headers
#endif

#include <cstring> // For memset.
#include <windows.h>
#include <winsock.h>
#include "resource.h"

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL\freeglut.h>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>

//#define STB_IMAGE_IMPLEMENTATION
//#include "stb_image.h"

//NatNet SDK
#include "NatNetTypes.h"
#include "NatNetCAPI.h"
#include "NatNetClient.h"
#include "natutils.h"

#include "GLPrint.h"
#include "RigidBodyCollection.h"
#include "MarkerPositionCollection.h"
#include "OpenGLDrawingFunctions.h"

#include <map>
#include <string>

#include <math.h>

#define ID_RENDERTIMER 101

#define MATH_PI 3.14159265F

// globals
// Class for printing bitmap fonts in OpenGL
GLPrint glPrinter;


// Creating a window



HINSTANCE hInst;  

// OpenGL rendering context.
// HGLRC is a type of OpenGL context
HGLRC openGLRenderContext = nullptr;

// Our NatNet client object.
NatNetClient natnetClient;

// Objects for saving off marker and rigid body data streamed
// from NatNet.
MarkerPositionCollection markerPositions;
RigidBodyCollection rigidBodies;

std::map<int, std::string> mapIDToName;

// Ready to render?
bool render = true;

// Show rigidbody info
bool showText = true;

// Used for converting NatNet data to the proper units.
float unitConversion = 1.0f;

// World Up Axis (default to Y)
int upAxis = 1; // 

// NatNet server IP address.
int IPAddress[4] = { 127, 0, 0, 1 };

// Timecode string 
char szTimecode[128] = "";

// Initial Eye position and rotation
float g_fEyeX = 0, g_fEyeY = 1, g_fEyeZ = 5;
float g_fRotY = 0;
float g_fRotX = 0;


// functions
// Win32
BOOL InitInstance(HINSTANCE, int);
// HWND-A handle to the window; UINT-The message; WPARAM-Additional message information; LPARAM-Same as WPARAM;
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);  
LRESULT CALLBACK NatNetDlgProc(HWND, UINT, WPARAM, LPARAM);
// OpenGL
void RenderOGLScene();
void Update(HWND hWnd);
// NatNet
void NATNET_CALLCONV DataHandler(sFrameOfMocapData* data, void* pUserData);    // receives data from the server
void NATNET_CALLCONV MessageHandler(Verbosity msgType, const char* msg);      // receives NatNet error messages
bool InitNatNet(LPSTR szIPAddress, LPSTR szServerIPAddress, ConnectionType connType);
bool ParseRigidBodyDescription(sDataDescriptions* pDataDefs);

//****************************************************************************
//
// Windows Functions 
//

// Register our window.
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEX wcex;
    wcex.cbSize = sizeof(WNDCLASSEX); // Sizeof, used when actual size must be known
    wcex.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wcex.lpfnWndProc = (WNDPROC)WndProc;
    wcex.cbClsExtra = 0;
    wcex.cbWndExtra = 0;
    wcex.hInstance = hInstance;
    wcex.hIcon = NULL;
    wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName = MAKEINTRESOURCE(IDC_NATNETSAMPLE);
    wcex.lpszClassName = "NATNETSAMPLE";
    wcex.hIconSm = NULL;

    return RegisterClassEx(&wcex);
}


////////////////////////////////////////////////////////////////////////////////////
//Load texture
GLuint texGround;
#define BMP_Header_Length 54  
static GLfloat angle = 0.0f;

int power_of_two(int n)
{
	if (n <= 0)
		return 0;
	return (n & (n - 1)) == 0;
}
GLuint load_texture(const char* file_name)
{
	GLint width, height, total_bytes;
	GLubyte* pixels = 0;
	GLuint last_texture_ID = 0, texture_ID = 0;
	FILE *pFile;

	// If loading fails, return 
	pFile = fopen(file_name, "rb");
	if (pFile == 0)
		return 0;

	// Read the length and width of the image
	fseek(pFile, 0x0012, SEEK_SET);
	fread(&width, 4, 1, pFile);
	fread(&height, 4, 1, pFile);
	fseek(pFile, BMP_Header_Length, SEEK_SET);

	
	{
		GLint line_bytes = width * 3;
		while (line_bytes % 4 != 0)
			++line_bytes;
		total_bytes = line_bytes * height;
	}

	
	pixels = (GLubyte*)malloc(total_bytes);
	if (pixels == 0)
	{
		fclose(pFile);
		return 0;
	}

	// Read pixel information
	if (fread(pixels, total_bytes, 1, pFile) <= 0)
	{
		free(pixels);
		fclose(pFile);
		return 0;
	}

	// 对就旧版本的兼容，如果图象的宽度和高度不是的整数次方，则需要进行缩放
	// 若图像宽高超过了OpenGL规定的最大值，也缩放
	{
		GLint max;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max);
		if (!power_of_two(width)
			|| !power_of_two(height)
			|| width > max
			|| height > max)
		{
			const GLint new_width = 256;
			const GLint new_height = 256; // 规定缩放后新的大小为边长的正方形
			GLint new_line_bytes, new_total_bytes;
			GLubyte* new_pixels = 0;

			// 计算每行需要的字节数和总字节数
			new_line_bytes = new_width * 3;
			while (new_line_bytes % 4 != 0)
				++new_line_bytes;
			new_total_bytes = new_line_bytes * new_height;

			// 分配内存
			new_pixels = (GLubyte*)malloc(new_total_bytes);
			if (new_pixels == 0)
			{
				free(pixels);
				fclose(pFile);
				return 0;
			}

			// 进行像素缩放
			gluScaleImage(GL_RGB,
				width, height, GL_UNSIGNED_BYTE, pixels,
				new_width, new_height, GL_UNSIGNED_BYTE, new_pixels);

			// 释放原来的像素数据，把pixels指向新的像素数据，并重新设置width和height
			free(pixels);
			pixels = new_pixels;
			width = new_width;
			height = new_height;
		}
	}

	// 分配一个新的纹理编号
	glGenTextures(1, &texture_ID);
	if (texture_ID == 0)
	{
		free(pixels);
		fclose(pFile);
		return 0;
	}
	GLint lastTextureID = last_texture_ID;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &lastTextureID);
	glBindTexture(GL_TEXTURE_2D, texture_ID);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0,
		GL_BGR_EXT, GL_UNSIGNED_BYTE, pixels);
	glBindTexture(GL_TEXTURE_2D, lastTextureID);  
	free(pixels);
	return texture_ID;
}
/////////////////////////////////////////////////////////////////////////////////////


// WinMain
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPSTR lpCmdLine, int nCmdShow)
{
    MyRegisterClass(hInstance);

    if (!InitInstance(hInstance, nCmdShow))
        return false;

    MSG msg;
    while (true)
    {
        if (PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE))
        {
            if (!GetMessage(&msg, NULL, 0, 0))
                break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            if (render)
                Update(msg.hwnd);
        }
    }

    return (int)msg.wParam;
}

// Initialize new instances of our application
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
    hInst = hInstance;

    HWND hWnd = CreateWindow("NATNETSAMPLE", "SampleClient 3D", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, NULL, NULL, hInstance, NULL);
	// createwindow(lpClassName, lpWindowName, dwstyle[overlapped window/ pop-up window/ child window], x, y, nWidth, nHeight, hWndParent, hM)

    if (!hWnd)
        return false;

    // Define pixel format; create an OpenGl rendering context
    PIXELFORMATDESCRIPTOR pfd;
    int nPixelFormat;
    memset(&pfd, NULL, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;

    // Set pixel format. Needed for drawing OpenGL bitmap fonts.
    HDC hDC = GetDC(hWnd);
    nPixelFormat = ChoosePixelFormat(hDC, &pfd);  // Convert the PIXELFORMATDESCRIPTOR into a pixel format number
	                                              // that represents the closest match it can find in the list of supported pixel formats
    SetPixelFormat(hDC, nPixelFormat, &pfd);      // Set the pixel number into the DC, this function takes the DC, the pixel format number, and a PFD struct pointer


    // Create and set the current OpenGL rendering context.
    openGLRenderContext = wglCreateContext(hDC);  // This function takes the DC as a parameter and returns a handle to the the OpenGL context 
	                                              // (of type HGLRC, for handle to GL Rendering Context).
    wglMakeCurrent(hDC, openGLRenderContext);     // Before using OpenGL, the created context must be current.

    // Set some OpenGL options.
    glClearColor(0.400f, 0.400f, 0.400f, 1.0f);
    glEnable(GL_LIGHT0);
    glEnable(GL_LIGHTING);
    glEnable(GL_DEPTH_TEST);   // Enable the depth buffer for depth test
	glEnable(GL_TEXTURE_2D);
	texGround = load_texture("D:\\Program Files (x86)\\Github\\SampleClient3D\\SampleClient3D\\Texture.bmp");

    // Set the device context for our OpenGL printer object.
    glPrinter.SetDeviceContext(hDC);

    wglMakeCurrent(0, 0);
    ReleaseDC(hWnd, hDC);

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // Make a good guess as to the IP address of our NatNet server.
    in_addr MyAddress[10];
    int nAddresses = NATUtils::GetLocalIPAddresses((unsigned long *)&MyAddress, 10);
    if (nAddresses > 0)
    {
        IPAddress[0] = MyAddress[0].S_un.S_un_b.s_b1;
        IPAddress[1] = MyAddress[0].S_un.S_un_b.s_b2;
        IPAddress[2] = MyAddress[0].S_un.S_un_b.s_b3;
        IPAddress[3] = MyAddress[0].S_un.S_un_b.s_b4;
    }

    // schedule to render on UI thread every 30 milliseconds； UI thread is the main thread in the application
    UINT renderTimer = SetTimer(hWnd, ID_RENDERTIMER, 30, NULL);
	// (Handle to main window, timer identifier, time interval, no timer callback)

    return true;
}


// Windows message processing function.
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    int wmId, wmEvent;
    PAINTSTRUCT ps;
    HDC hdc;

    switch (message)     // The message parameter contains the message sent
    {
    case WM_COMMAND:     // Handle menu selections, etc.
        wmId = LOWORD(wParam);
        wmEvent = HIWORD(wParam);
        // Parse the menu selections:
        switch (wmId)
        {
        case IDM_CONNECT:
            DialogBox(hInst, (LPCTSTR)IDD_NATNET, hWnd, (DLGPROC)NatNetDlgProc); 
			// (A module which contains the dialog box template;
			// The dialog box template;
			// A handle to the window that owns the dialog box;
			// A pointer to the dialog box procedure)
            break;
        case IDM_EXIT:
            DestroyWindow(hWnd);
            break;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
			// Calls the default window procedure to provide default processing for any window messages that an application does not process.
			// This function ensures that every message is processed.
        }
        break;

    case WM_TIMER:    // To process messages the WM_TIMER messages generated by the timer ID_RENDERTIMER
        if (wParam == ID_RENDERTIMER)
            Update(hWnd);
        break;


		// Change the angel of the view

    case WM_KEYDOWN: // KEYUP ALSO； 
		// Keystroke message
    {
        bool bShift = (GetKeyState(VK_SHIFT) & 0x80) != 0; // nVirtKey-	Determines how to process the keystroke
        bool bCtrl = (GetKeyState(VK_CONTROL) & 0x80) != 0;
        switch (wParam)
        {
        case VK_UP:  // Process the UP ARROW key
            if (bCtrl)
                g_fRotX += 1;
            else if (bShift)
                g_fEyeY += 0.03f;
            else
                g_fEyeZ -= 0.03f;
            break;
        case VK_DOWN:
            if (bCtrl)
                g_fRotX -= 1;
            else if (bShift)
                g_fEyeY -= 0.03f;
            else
                g_fEyeZ += 0.03f;
            break;
        case VK_LEFT:
            if (bCtrl)
                g_fRotY += 1;
            else
                g_fEyeX -= 0.03f;
            break;
        case VK_RIGHT:
            if (bCtrl)
                g_fRotY -= 1;
            else
                g_fEyeX += 0.03f;
            break;
        case 'T':
        case 't':
            showText = !showText;
            break;
        }
        InvalidateRect(hWnd, NULL, TRUE);
    }
        break;

    case WM_PAINT:   // An application makes a request to paint a portion of an application's window
        hdc = BeginPaint(hWnd, &ps);  // Retrieve the display device context
        Update(hWnd);
        EndPaint(hWnd, &ps);
        break;

    case WM_SIZE:  // Set display resolution or resizing requested
    {
        int cx = LOWORD(lParam), cy = HIWORD(lParam);
        if (cx != 0 && cy != 0 && hWnd != nullptr)
        {
            GLfloat fFovy = 40.0f; // Field-of-view
            GLfloat fZNear = 1.0f;  // Near clipping plane
            GLfloat fZFar = 10000.0f;  // Far clipping plane

            HDC hDC = GetDC(hWnd);
            wglMakeCurrent(hDC, openGLRenderContext);

            // Calculate OpenGL viewport aspect
            RECT rv;
            GetClientRect(hWnd, &rv);  // Retrieve the coordinates of a window's client area, which specify the upper-left and lower-right corners. 
			                           // The upper-left i (0,0)
            GLfloat fAspect = (GLfloat)(rv.right - rv.left) / (GLfloat)(rv.bottom - rv.top);   // Aspect ratio = width / height

            // Define OpenGL viewport
            glMatrixMode(GL_PROJECTION);  // Transform eye space coordinates into clip coordinates
            glLoadIdentity();
            gluPerspective(fFovy, fAspect, fZNear, fZFar); // Sets up a perspective projection matrix
			// (The field of view angle, in degrees, in the y-direction; 
			// Determines the field of view in the x-direction. The aspect ratio is the ratio of x (width) to y (height);
			// The distance from the viewer to the near clipping plane (always positive);
			// The distance from the viewer to the far clipping plane (always positive))
            glViewport(rv.left, rv.top, rv.right - rv.left, rv.bottom - rv.top);
            glMatrixMode(GL_MODELVIEW); 
			// Viewing transformation, OpenGL moves the scene with the inverse of the camera transformation//
			// Transform object space coordinates into eye space coordinates,
			// Think of the projection matrix as describing the attributes of your camera, 
			// such as field of view, focal length, fish eye lens, etc. 
			// Think of the ModelView matrix as where you stand with the camera and the direction you point it.//

            Update(hWnd);

            wglMakeCurrent(0, 0);
            ReleaseDC(hWnd, hDC);
        }
    }
        break;

    case WM_DESTROY:
    {
        HDC hDC = GetDC(hWnd);
        wglMakeCurrent(hDC, openGLRenderContext);
        natnetClient.Disconnect();
        wglMakeCurrent(0, 0);
        wglDeleteContext(openGLRenderContext);
        ReleaseDC(hWnd, hDC);
        PostQuitMessage(0);
    }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    return 0;
}






// Update OGL window
void Update(HWND hwnd)
{
    HDC hDC = GetDC(hwnd);  // FOR DISPLAY: To carry out drawing that must occur instantly rather than when a WM_PAINT message is processing
	// Such drawing is usually in response to an action by the user, such as making a selection or drawing with the mouse,
	// In such cases, the user should receive instant feedback and must not be forced to stop selecting or drawing in order for the application to display the result
    if (hDC)
    {
        wglMakeCurrent(hDC, openGLRenderContext);
        RenderOGLScene();
        SwapBuffers(hDC);   // Switch the front and back buffer	
        wglMakeCurrent(0, 0);
    }
    ReleaseDC(hwnd, hDC);
}

void ConvertRHSPosZupToYUp(float& x, float& y, float& z)
{
    /*
    [RHS, Y-Up]     [RHS, Z-Up]

                          Y
     Y                 Z /
     |__ X             |/__ X
     /
    Z

    Xyup  =  Xzup
    Yyup  =  Zzup
    Zyup  =  -Yzup
    */
    float yOriginal = y;
    y = z;
    z = -yOriginal;
}

void ConvertRHSRotZUpToYUp(float& qx, float& qy, float& qz, float& qw)
{
    // -90 deg rotation about +X
    float qRx, qRy, qRz, qRw;
    float angle = -90.0f * MATH_PI / 180.0f;
    qRx = sin(angle / 2.0f);
    qRy = 0.0f;
    qRz = 0.0f;
    qRw = cos(angle / 2.0f);

    // rotate quat using quat multiply
    float qxNew, qyNew, qzNew, qwNew;
    qxNew = qw*qRx + qx*qRw + qy*qRz - qz*qRy;
    qyNew = qw*qRy - qx*qRz + qy*qRw + qz*qRx;
    qzNew = qw*qRz + qx*qRy - qy*qRx + qz*qRw;
    qwNew = qw*qRw - qx*qRx - qy*qRy - qz*qRz;

    qx = qxNew;
    qy = qyNew;
    qz = qzNew;
    qw = qwNew;
}

// Render OpenGL scene
void RenderOGLScene()
{
    GLfloat m[9];
    GLfloat v[3];
    float fRadius = 5.0f;

    // Setup OpenGL viewport
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // Clear buffers
    glLoadIdentity(); // Load identity matrix
    GLfloat glfLightPos[] = { -4.0f, 4.0f, 4.0f, 0.0f };   // Specify the position of the light in homogeneous object coordinates
	// The forth parameter w is 0.0, which means the light is treated as an directional source
    GLfloat glfLightAmb[] = { .3f, .3f, .3f, 1.0f };  // Specify the ambient RGBA intensity of the light
    glLightfv(GL_LIGHT0, GL_AMBIENT, glfLightAmb);  // The light position is transformed by the modelview matrix and stored in eye coordinates
    glLightfv(GL_LIGHT1, GL_POSITION, glfLightPos);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);  // Causes a material color to track the current color
	// Both the front and back material parameters and ambient and diffuse material parameters track the current color
    glPushMatrix();   // Save the current matrix stack


    // Draw timecode
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);  // Set the current color(red, green, blue, alpha-opacity)
    glPushMatrix();
    glTranslatef(2400.f, -1750.f, -5000.0f);  // Multiplies the current matrix by a translation matrix(x, y, z)
    glPrinter.Print(0.0f, 0.0f, szTimecode);
    glPopMatrix();

    // Position and rotate the camera
    glTranslatef(g_fEyeX * -1000, g_fEyeY * -1000, g_fEyeZ * -1000);
    glRotatef(g_fRotY, 0, 1, 0);   // Multiplies the current matrix by a rotation matrix
    glRotatef(g_fRotX, 1, 0, 0);
    

    // Draw reference axis triad
    // x
    glLineWidth(3.0f);
    glBegin(GL_LINES);
    glColor3f(.8f, 0.0f, 0.0f);
    glVertex3f(0, 0, 0);
    glVertex3f(300, 0, 0);
    // y
    glColor3f(0.0f, .8f, 0.0f);
    glVertex3f(0, 0, 0);
    glVertex3f(0, 300, 0);
    // z
    glColor3f(0.0f, 0.0f, .8f);
    glVertex3f(0, 0, 0);
    glVertex3f(0, 0, 300);
    glEnd();


	//////////////////// Set the ground image ///////////////////////////////////////////////

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texGround);
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f); glVertex3f(-8.0f, -8.0f, 0.0f);
	glTexCoord2f(0.0f, 3.0f); glVertex3f(-8.0f, 8.0f, 0.0f);
	glTexCoord2f(3.0f, 3.0f); glVertex3f(8.0f, 8.0f, 0.0f);
	glTexCoord2f(3.0f, 0.0f); glVertex3f(8.0f, -8.0f, 0.0f);
	glEnd();
	/*glutSwapBuffers();*/
	//////////////////////////////////////////////////////////////////////////////////////////


     //Draw grid
    glLineWidth(1.0f);
    OpenGLDrawingFunctions::DrawGrid();

    // Draw rigid bodies
    float textX = -3200.0f;
    float textY = 2700.0f;
    GLfloat x, y, z;
    Quat q;
    EulerAngles ea;
    int order;

    for (size_t i = 0; i < rigidBodies.Count(); i++)
    {
        // RigidBody position
        std::tie(x, y, z) = rigidBodies.GetCoordinates(i);
        // convert to millimeters
        x *= unitConversion;
        y *= unitConversion;
        z *= unitConversion;

        // RigidBody orientation
        GLfloat qx, qy, qz, qw;
        std::tie(qx, qy, qz, qw) = rigidBodies.GetQuaternion(i);
        q.x = qx;
        q.y = qy;
        q.z = qz;
        q.w = qw;

        // If Motive is streaming Z-up, convert to this renderer's Y-up coordinate system
        if (upAxis==2)
        {
            // convert position
            ConvertRHSPosZupToYUp(x, y, z);
            // convert orientation
            ConvertRHSRotZUpToYUp(q.x, q.y, q.z, q.w);
        }

        // Convert Motive quaternion output to euler angles
        // Motive coordinate conventions : X(Pitch), Y(Yaw), Z(Roll), Relative, RHS
        order = EulOrdXYZr;
        ea = Eul_FromQuat(q, order);

        ea.x = NATUtils::RadiansToDegrees(ea.x);
        ea.y = NATUtils::RadiansToDegrees(ea.y);
        ea.z = NATUtils::RadiansToDegrees(ea.z);

        // Draw RigidBody as cube
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glPushMatrix();

        glTranslatef(x, y, z);

        // source is Y-Up (default)
        glRotatef(ea.x, 1.0f, 0.0f, 0.0f);
        glRotatef(ea.y, 0.0f, 1.0f, 0.0f);
        glRotatef(ea.z, 0.0f, 0.0f, 1.0f);

        /*
        // alternate Z-up conversion - convert only euler rotation interpretation
        //  Yyup  =  Zzup
        //  Zyup  =  -Yzup
        glRotatef(ea.x, 1.0f, 0.0f, 0.0f);
        glRotatef(ea.y, 0.0f, 0.0f, 1.0f);
        glRotatef(ea.z, 0.0f, -1.0f, 0.0f);
        */

        OpenGLDrawingFunctions::DrawCube(100.0f);
        glPopMatrix();
        glPopAttrib();

        if (showText)
        {
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            std::string rigidBodyName = mapIDToName.at(rigidBodies.ID(i));
            glPrinter.Print(textX, textY, "%s (Pitch: %3.1f, Yaw: %3.1f, Roll: %3.1f)", rigidBodyName.c_str(), ea.x, ea.y, ea.z);
            textY -= 100.0f;
        }

    }

    // Draw unlabeled markers (orange)
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glColor4f(0.8f, 0.4f, 0.0f, 0.8f);
    fRadius = 20.0f;
    for (size_t i = 0; i < markerPositions.MarkerPositionCount(); i++)
    {
        std::tie(v[0], v[1], v[2]) = markerPositions.GetMarkerPosition(i);

        // If Motive is streaming Z-up, convert to this renderer's Y-up coordinate system
        if (upAxis == 2)
        {
            ConvertRHSPosZupToYUp(v[0], v[1], v[2]);
        }

        // [optional] local coordinate support : inherit (accumulate) parent's RigidBody position & orientation ("root transform") if using local marker positions
        // typically used with face capture setups
        if (rigidBodies.Count() == 1)
        {
            NATUtils::Vec3MatrixMult(v, m);
            v[0] += std::get<0>(rigidBodies.GetCoordinates(0));
            v[1] += std::get<1>(rigidBodies.GetCoordinates(0));
            v[2] += std::get<2>(rigidBodies.GetCoordinates(0));
        }

        // convert to millimeters
        v[0] *= unitConversion;
        v[1] *= unitConversion;
        v[2] *= unitConversion;

        glPushMatrix();
        glTranslatef(v[0], v[1], v[2]);
        OpenGLDrawingFunctions::DrawSphere(1, fRadius);
        glPopMatrix();
    }
    glPopAttrib();

    // Draw labeled markers (white)
    glPushAttrib(GL_ALL_ATTRIB_BITS);
    glColor4f(0.8f, 0.8f, 0.8f, 0.8f);
    for (size_t i = 0; i < markerPositions.LabeledMarkerPositionCount(); i++)
    {
        const sMarker& markerData = markerPositions.GetLabeledMarker(i);
        v[0] = markerData.x * unitConversion;
        v[1] = markerData.y * unitConversion;
        v[2] = markerData.z * unitConversion;
        fRadius = markerData.size * unitConversion;

        // If Motive is streaming Z-up, convert to this renderer's Y-up coordinate system
        if (upAxis == 2)
        {
            ConvertRHSPosZupToYUp(v[0], v[1], v[2]);
        }

        glPushMatrix();
        glTranslatef(v[0], v[1], v[2]);
        OpenGLDrawingFunctions::DrawSphere(1, fRadius);
        glPopMatrix();

    }
    glPopAttrib();

    glPopMatrix();
    glFlush();

    // Done rendering a frame. The NatNet callback function DataHandler
    // will set render to true when it receives another frame of data.
    render = false;

}

// Callback for the connect-to-NatNet dialog. Gets the server and local IP 
// addresses and attempts to initialize the NatNet client.
LRESULT CALLBACK NatNetDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    char szBuf[512];
    switch (message)
    {
    case WM_INITDIALOG:
        SetDlgItemText(hDlg, IDC_EDIT1, _itoa(IPAddress[0], szBuf, 10));
        SetDlgItemText(hDlg, IDC_EDIT2, _itoa(IPAddress[1], szBuf, 10));
        SetDlgItemText(hDlg, IDC_EDIT3, _itoa(IPAddress[2], szBuf, 10));
        SetDlgItemText(hDlg, IDC_EDIT4, _itoa(IPAddress[3], szBuf, 10));
        SetDlgItemText(hDlg, IDC_EDIT5, _itoa(IPAddress[0], szBuf, 10));
        SetDlgItemText(hDlg, IDC_EDIT6, _itoa(IPAddress[1], szBuf, 10));
        SetDlgItemText(hDlg, IDC_EDIT7, _itoa(IPAddress[2], szBuf, 10));
        SetDlgItemText(hDlg, IDC_EDIT8, _itoa(IPAddress[3], szBuf, 10));
        SendDlgItemMessage( hDlg, IDC_COMBO_CONNTYPE, CB_ADDSTRING, 0, (LPARAM)TEXT( "Multicast" ) );
        SendDlgItemMessage( hDlg, IDC_COMBO_CONNTYPE, CB_ADDSTRING, 0, (LPARAM)TEXT( "Unicast" ) );
        SendDlgItemMessage( hDlg, IDC_COMBO_CONNTYPE, CB_SETCURSEL, 0, 0 );
        return true;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_CONNECT:
        {
            char szMyIPAddress[30], szServerIPAddress[30];
            char ip1[5], ip2[5], ip3[5], ip4[5];
            GetDlgItemText(hDlg, IDC_EDIT1, ip1, 4);
            GetDlgItemText(hDlg, IDC_EDIT2, ip2, 4);
            GetDlgItemText(hDlg, IDC_EDIT3, ip3, 4);
            GetDlgItemText(hDlg, IDC_EDIT4, ip4, 4);
            sprintf_s(szMyIPAddress, 30, "%s.%s.%s.%s", ip1, ip2, ip3, ip4);

            GetDlgItemText(hDlg, IDC_EDIT5, ip1, 4);
            GetDlgItemText(hDlg, IDC_EDIT6, ip2, 4);
            GetDlgItemText(hDlg, IDC_EDIT7, ip3, 4);
            GetDlgItemText(hDlg, IDC_EDIT8, ip4, 4);
            sprintf_s(szServerIPAddress, 30, "%s.%s.%s.%s", ip1, ip2, ip3, ip4);

            const ConnectionType connType = (ConnectionType)SendDlgItemMessage( hDlg, IDC_COMBO_CONNTYPE, CB_GETCURSEL, 0, 0 );

            // Try and initialize the NatNet client.
            if (InitNatNet( szMyIPAddress, szServerIPAddress, connType ) == false)
            {
                natnetClient.Disconnect();
                MessageBox(hDlg, "Failed to connect", "", MB_OK);
            }
        }
        case IDOK:
        case IDCANCEL:
            EndDialog(hDlg, LOWORD(wParam));
            return true;
        }
    }
    return false;
}

// Initialize the NatNet client with client and server IP addresses.
bool InitNatNet( LPSTR szIPAddress, LPSTR szServerIPAddress, ConnectionType connType )
{
    unsigned char ver[4];
    NatNet_GetVersion(ver);

    // Set callback handlers
    // Callback for NatNet messages.
    NatNet_SetLogCallback( MessageHandler );
    // this function will receive data from the server
    natnetClient.SetFrameReceivedCallback(DataHandler);

    sNatNetClientConnectParams connectParams;
    connectParams.connectionType = connType;
    connectParams.localAddress = szIPAddress;
    connectParams.serverAddress = szServerIPAddress;
    int retCode = natnetClient.Connect( connectParams );
    if (retCode != ErrorCode_OK)
    {
        //Unable to connect to server.
        return false;
    }
    else
    {
        // Print server info
        sServerDescription ServerDescription;
        memset(&ServerDescription, 0, sizeof(ServerDescription));
        natnetClient.GetServerDescription(&ServerDescription);
        if (!ServerDescription.HostPresent)
        {
            //Unable to connect to server. Host not present
            return false;
        }
    }

    // Retrieve RigidBody description from server
    sDataDescriptions* pDataDefs = NULL;
    retCode = natnetClient.GetDataDescriptionList(&pDataDefs);
    if (retCode != ErrorCode_OK || ParseRigidBodyDescription(pDataDefs) == false)
    {
        //Unable to retrieve RigidBody description
        //return false;
    }
    NatNet_FreeDescriptions( pDataDefs );
    pDataDefs = NULL;

    // example of NatNet general message passing. Set units to millimeters
    // and get the multiplicative conversion factor in the response.
    void* response;
    int nBytes;
    retCode = natnetClient.SendMessageAndWait("UnitsToMillimeters", &response, &nBytes);
    if (retCode == ErrorCode_OK)
    {
        unitConversion = *(float*)response;
    }

    retCode = natnetClient.SendMessageAndWait("UpAxis", &response, &nBytes);
    if (retCode == ErrorCode_OK)
    {
        upAxis = *(long*)response;
    }

    return true;
}

bool ParseRigidBodyDescription(sDataDescriptions* pDataDefs)
{
    mapIDToName.clear();

    if (pDataDefs == NULL || pDataDefs->nDataDescriptions <= 0)
        return false;

    // preserve a "RigidBody ID to Rigid Body Name" mapping, which we can lookup during data streaming
    int iSkel = 0;
    for (int i = 0, j = 0; i < pDataDefs->nDataDescriptions; i++)
    {
        if (pDataDefs->arrDataDescriptions[i].type == Descriptor_RigidBody)
        {
            sRigidBodyDescription *pRB = pDataDefs->arrDataDescriptions[i].Data.RigidBodyDescription;
            mapIDToName[pRB->ID] = std::string(pRB->szName);
        }
        else if (pDataDefs->arrDataDescriptions[i].type == Descriptor_Skeleton)
        {
            sSkeletonDescription *pSK = pDataDefs->arrDataDescriptions[i].Data.SkeletonDescription;
            for (int i = 0; i < pSK->nRigidBodies; i++)
            {
                // Note: Within FrameOfMocapData, skeleton rigid body ids are of the form:
                //   parent skeleton ID   : high word (upper 16 bits of int)
                //   rigid body id        : low word  (lower 16 bits of int)
                // 
                // However within DataDescriptions they are not, so apply that here for correct lookup during streaming
                int id = pSK->RigidBodies[i].ID | (pSK->skeletonID << 16);
                mapIDToName[id] = std::string(pSK->RigidBodies[i].szName);
            }
            iSkel++;
        }
        else
            continue;
    }

    return true;
}

// [Optional] Handler for NatNet messages. 
void NATNET_CALLCONV MessageHandler(Verbosity msgType, const char* msg)
{
    //	printf("\n[SampleClient] Message received: %s\n", msg);
}

// NatNet data callback function. Stores rigid body and marker data in the file level 
// variables markerPositions, and rigidBodies and sets the file level variable render
// to true. This signals that we have a frame ready to render.
void DataHandler(sFrameOfMocapData* data, void* pUserData)
{
    int mcount = min(MarkerPositionCollection::MAX_MARKER_COUNT, data->MocapData->nMarkers);
    markerPositions.SetMarkerPositions(data->MocapData->Markers, mcount);

    // labeled markers
    markerPositions.SetLabledMarkers(data->LabeledMarkers, data->nLabeledMarkers);

    // unlabeled markers
    mcount = min(MarkerPositionCollection::MAX_MARKER_COUNT, data->nOtherMarkers);
    markerPositions.AppendMarkerPositions(data->OtherMarkers, mcount);

    // rigid bodies
    int rbcount = min(RigidBodyCollection::MAX_RIGIDBODY_COUNT, data->nRigidBodies);
    rigidBodies.SetRigidBodyData(data->RigidBodies, rbcount);

    // skeleton segment (bones) as collection of rigid bodies
    for (int s = 0; s < data->nSkeletons; s++)
    {
        rigidBodies.AppendRigidBodyData(data->Skeletons[s].RigidBodyData, data->Skeletons[s].nRigidBodies);
    }

    // timecode
    NatNetClient* pClient = (NatNetClient*)pUserData;
    int hour, minute, second, frame, subframe;
    NatNet_DecodeTimecode( data->Timecode, data->TimecodeSubframe, &hour, &minute, &second, &frame, &subframe );
    // decode timecode into friendly string
    NatNet_TimecodeStringify( data->Timecode, data->TimecodeSubframe, szTimecode, 128 );

    render = true;
}
