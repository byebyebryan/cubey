
#define GLM_FORCE_PURE

#include "GL/glew.h"
#include "GLFW/glfw3.h"
#include "glm/glm.hpp"
#include "glm/ext.hpp"
#include "AntTweakBar.h"

#include <stdlib.h>
#include <stdio.h>

#include <iostream>
#include <thread>
#include <chrono>

#include "Shader.h"

float pFov = 50.0f;
float pAspect = 1.0f;
float pNear = 1.0f;
float pFar = 1000.0f;

static void error_callback(int error, const char* description)
{
	fputs(description, stderr);
}

void windowSize_callback(GLFWwindow* window, int width, int height)
{
	glfwSetWindowSize(window, width, height);

	pAspect = width / (float) height;

	TwWindowSize(width, height);
}

void TwEventMouseButtonGLFW3(GLFWwindow* window, int button, int action, int mods)
{
	TwEventMouseButtonGLFW(button, action);
}

void TwEventMousePosGLFW3(GLFWwindow* window, double xpos, double ypos)
{
	TwEventMousePosGLFW(xpos, ypos);
}

void TwEventMouseWheelGLFW3(GLFWwindow* window, double xoffset, double yoffset)
{
	TwEventMouseWheelGLFW(yoffset);
}

void TwEventKeyGLFW3(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	TwEventKeyGLFW(key, action);
	if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
		glfwSetWindowShouldClose(window, GL_TRUE);
}

void TwEventCharGLFW3(GLFWwindow* window, unsigned int codepoint)
{
	TwEventCharGLFW(codepoint, GLFW_PRESS);
}

int main(void)
{
	GLFWwindow* window;
	glfwSetErrorCallback(error_callback);
	if (!glfwInit())
		exit(EXIT_FAILURE);
	window = glfwCreateWindow(640, 480, "Simple example", NULL, NULL);
	if (!window)
	{
		glfwTerminate();
		exit(EXIT_FAILURE);
	}
	glfwMakeContextCurrent(window);

	//glfwSwapInterval(1);

	TwInit(TW_OPENGL, NULL);
	glfwSetWindowSizeCallback(window, windowSize_callback);
	glfwSetMouseButtonCallback(window, (GLFWmousebuttonfun)TwEventMouseButtonGLFW3);
	glfwSetCursorPosCallback(window, (GLFWcursorposfun)TwEventMousePosGLFW3);
	glfwSetScrollCallback(window, (GLFWscrollfun)TwEventMouseWheelGLFW3);
	glfwSetKeyCallback(window, (GLFWkeyfun)TwEventKeyGLFW3);
	glfwSetCharCallback(window, (GLFWcharfun)TwEventCharGLFW3);
	
	//glfwSwapInterval(1);

	TwBar *bar = TwNewBar("TweakBar");
	TwWindowSize(640, 480);
	TwDefine(" GLOBAL help='This example shows how to integrate AntTweakBar with GLFW and OpenGL.' ");

	double dt = 0.0;
	double fps = 60.0;
	double time = glfwGetTime();
	TwAddVarRO(bar, "time", TW_TYPE_DOUBLE, &time, " label='Time' precision=2 help='Time (in seconds).' ");
	TwAddVarRO(bar, "delta time", TW_TYPE_DOUBLE, &dt, " label='Delta Time' precision=2 help='Delta Time (in seconds).' ");
	TwAddVarRO(bar, "FPS", TW_TYPE_DOUBLE, &fps, " label='FPS' precision=2 help='Frame Per Second' ");

	glewInit();

	cubey::ShaderProgram prog;
	prog.AddShader(GL_VERTEX_SHADER, "shaders\\debug_shader.glsl", "#define _VERTEX_S_");
	prog.AddShader(GL_FRAGMENT_SHADER, "shaders\\debug_shader.glsl", "#define _FRAGMENT_S_");
	prog.LinkProgram();

	GLint max_buffer_size;
	glGetProgramiv(prog.gl_, GL_ACTIVE_UNIFORM_MAX_LENGTH, &max_buffer_size);

	GLint uniform_size;
	GLenum uniform_type;
	char* uniform_name = new char[max_buffer_size];
	glGetActiveUniform(prog.gl_, 0, max_buffer_size, NULL, &uniform_size, &uniform_type, uniform_name);

	delete[] uniform_name;

	GLint uniform_loc0 = glGetUniformLocation(prog.gl_, "u_projection_mat");
	GLint uniform_loc1 = glGetUniformLocation(prog.gl_, "u_model_mat");

	while (!glfwWindowShouldClose(window))
	{
		float ratio;
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		ratio = width / (float)height;
		glViewport(0, 0, width, height);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		float top = pNear * glm::tan(0.5f * glm::radians(pFov));
		float right = top * pAspect;

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glFrustum(-right, right, -top, top, pNear, pFar);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
		//glRotatef((float)glfwGetTime() * 50.f, 0.f, 0.f, 1.f);
		glBegin(GL_TRIANGLES);
		glColor3f(1.f, 0.f, 0.f);
		glVertex3f(-6.f, -4.f, -100.f);
		glColor3f(0.f, 1.f, 0.f);
		glVertex3f(6.f, -4.f, -100.f);
		glColor3f(0.f, 0.f, 1.f);
		glVertex3f(0.f, 6.f, -100.f);
		glEnd();

		double t0 = time;
		time = glfwGetTime();
		dt = time - t0;

		double t1 = 1.0 / 60.0 - dt;
		if (t1 > 0)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds((int)(t1*1000.0)));
			t0 = time;
			time = glfwGetTime();
			dt = time - t0;
		}

		fps = 1.0 / dt;

		TwDraw();

		glfwSwapBuffers(window);
		glfwPollEvents();
	}
	glfwDestroyWindow(window);
	TwTerminate();
	glfwTerminate();
	exit(EXIT_SUCCESS);
}