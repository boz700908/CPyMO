#include "cpymo_tool_prelude.h"
#include "cpymo_tool_ffmpeg.h"
#include <stdio.h>
#include <string.h>

error_t cpymo_tool_ffmpeg_search(const char **out_ffmpeg_command)
{
    #define TRY(CMD) \
        if (system(CMD " -version") == 0) { \
            *out_ffmpeg_command = CMD; \
            return CPYMO_ERR_SUCC; \
        } \

    #ifdef _WIN32
    TRY("ffmpeg");
    TRY("cmd /c ffmpeg");
    TRY("powershell ./ffmpeg");
    #else
    TRY("./ffmpeg");
    TRY("ffmpeg");
    #endif

    #undef TRY

    return CPYMO_ERR_NOT_FOUND;
}

error_t cpymo_tool_ffmpeg_call(
    const char *ffmpeg_command,
    const char *src,
    const char *dst,
    const char *fmt,
    const char *flags)
{
    size_t flags_len = 0;
    if (flags) flags_len = 1 + strlen(flags);

    size_t cmd_len = strlen(ffmpeg_command)
        + strlen(src)
        + strlen(dst)
        + strlen(fmt)
        + flags_len
        + 32;
    char *command = (char *)malloc(cmd_len);
    if (command == NULL) return CPYMO_ERR_OUT_OF_MEM;

    snprintf(command, cmd_len, "%s -i \"%s\" -y -v quiet -f %s %s%s\"%s\"",
        ffmpeg_command,
        src,
        fmt,
        flags == NULL ? "" : flags,
        flags == NULL ? "" : " ",
        dst);
    int rc = system(command);
    free(command);

    if (rc != 0) return CPYMO_ERR_UNKNOWN;

    return CPYMO_ERR_SUCC;
}
