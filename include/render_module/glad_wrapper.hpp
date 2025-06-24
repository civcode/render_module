#ifndef GLAD_WRAPPER_HPP_
#define GLAD_WRAPPER_HPP_

#include <glad/glad.h>

namespace glad {

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    ::glViewport(x, y, width, height);
}
void glClearColor(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) {
    ::glClearColor(red, green, blue, alpha);
}
void glClear(GLbitfield mask) {
    ::glClear(mask);
}

}

#endif // GLAD_WRAPPER_HPP_