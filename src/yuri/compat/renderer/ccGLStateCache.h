#pragma once

#include <vitaGL.h>

// Yuri's OpenGL compositor only needs Cocos' GL state-cache API, not its
// scene graph.  Keep these calls deliberately stateless: the Vita presenter
// and compositor share one VitaGL context, so replaying the requested state is
// both cheap and correct across that boundary.
namespace cocos2d::GL {

inline void activeTexture(GLenum texture) {
    glActiveTexture(texture);
}

inline void bindTexture2D(GLuint texture) {
    glBindTexture(GL_TEXTURE_2D, texture);
}

inline void bindTexture2DN(GLuint unit, GLuint texture) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}

inline void deleteTexture(GLuint texture) {
    if (texture) glDeleteTextures(1, &texture);
}

inline void useProgram(GLuint program) {
    glUseProgram(program);
}

inline void blendResetToCache() {
    glDisable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ONE, GL_ZERO);
}

inline void enableVertexAttribs(unsigned int flags) {
    // SGX543 exposes at most 16 generic attributes. Yuri's compositor uses
    // one position attribute and at most a handful of texture coordinates.
    for (unsigned int index = 0; index < 16; ++index) {
        if (flags & (1u << index))
            glEnableVertexAttribArray(index);
        else
            glDisableVertexAttribArray(index);
    }
}

} // namespace cocos2d::GL
