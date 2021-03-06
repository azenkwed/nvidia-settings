/* 
 * 
 * Copyright (c) 1997  Metro Link Incorporated
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"), 
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE X CONSORTIUM BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * Except as contained in this notice, the name of the Metro Link shall not be
 * used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from Metro Link.
 * 
 */
/*
 * Copyright (c) 1997-2003 by The XFree86 Project, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the copyright holder(s)
 * and author(s) shall not be used in advertising or otherwise to promote
 * the sale, use or other dealings in this Software without prior written
 * authorization from the copyright holder(s) and author(s).
 */



#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#if !defined(X_NOT_POSIX)
#if defined(_POSIX_SOURCE)
#include <limits.h>
#else
#define _POSIX_SOURCE
#include <limits.h>
#undef _POSIX_SOURCE
#endif /* _POSIX_SOURCE */
#endif /* !X_NOT_POSIX */
#if !defined(PATH_MAX)
#if defined(MAXPATHLEN)
#define PATH_MAX MAXPATHLEN
#else
#define PATH_MAX 1024
#endif /* MAXPATHLEN */
#endif /* !PATH_MAX */

#if !defined(MAXHOSTNAMELEN)
#define MAXHOSTNAMELEN 32
#endif /* !MAXHOSTNAMELEN */

#include "Configint.h"
#include "xf86tokens.h"

#define CONFIG_BUF_LEN     1024

static int StringToToken (char *, XConfigSymTabRec *);

static FILE *configFile = NULL;
static const char **builtinConfig = NULL;
static int builtinIndex = 0;
static int configPos = 0;            /* current readers position */
static char *configBuf, *configRBuf; /* buffer for lines */
static int pushToken = LOCK_TOKEN;
static int eol_seen = 0;             /* private state to handle comments */
LexRec val;

int configLineNo = 0;         /* linenumber */
char *configSection = NULL;   /* name of current section being parsed */
char *configPath;             /* path to config file */




static int xconfigIsAlpha(char c)
{
    return (((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')));
}

static int xconfigIsDigit(char c)
{
    return ((c >= '0') && (c <= '9'));
}

static int xconfigIsUpper(char c)
{
    return ((c >= 'A') && (c <= 'Z'));
}

static char xconfigToLower(char c)
{
    if ((c >= 'A') && (c <= 'Z')) {
        return c + ('a' - 'A');
    } else {
        return c;
    }
}


/* 
 * xconfigStrToUL --
 *
 *  A portable, but restricted, version of strtoul().  It only understands
 *  hex, octal, and decimal.  But it's good enough for our needs.
 */

static unsigned int xconfigStrToUL (char *str)
{
    int base = 10;
    char *p = str;
    unsigned int tot = 0;

    if (*p == '0')
    {
        p++;
        if ((*p == 'x') || (*p == 'X'))
        {
            p++;
            base = 16;
        }
        else
            base = 8;
    }
    while (*p)
    {
        if ((*p >= '0') && (*p <= ((base == 8) ? '7' : '9')))
        {
            tot = tot * base + (*p - '0');
        }
        else if ((base == 16) && (*p >= 'a') && (*p <= 'f'))
        {
            tot = tot * base + 10 + (*p - 'a');
        }
        else if ((base == 16) && (*p >= 'A') && (*p <= 'F'))
        {
            tot = tot * base + 10 + (*p - 'A');
        }
        else
        {
            return (tot);
        }
        p++;
    }
    return (tot);
}


/*
 * xconfigGetNextLine --
 *
 *  read from the configFile FILE stream until we encounter a new
 *  line; this is effectively just a big wrapper for fgets(3).
 *
 *  xconfigGetToken() assumes that we will read up to the next
 *  newline; we need to grow configBuf and configRBuf as needed to
 *  support that.
 */

static char *xconfigGetNextLine(void)
{
    static int configBufLen = CONFIG_BUF_LEN;
    char *tmpConfigBuf, *tmpConfigRBuf;
    int c, i, pos = 0, eolFound = 0;
    char *ret = NULL;
    
    /*
     * reallocate the string if it was grown last time (i.e., is no
     * longer CONFIG_BUF_LEN); we malloc the new strings first, so
     * that if either of the mallocs fail, we can fall back on the
     * existing buffer allocations
     */
    
    if (configBufLen != CONFIG_BUF_LEN) {
                 
        tmpConfigBuf = malloc(CONFIG_BUF_LEN);
        tmpConfigRBuf = malloc(CONFIG_BUF_LEN);
        
        if (!tmpConfigBuf || !tmpConfigRBuf) {
            
            /*
             * at least one of the mallocs failed; keep the old buffers
             * and free any partial allocations
             */
            
            free(tmpConfigBuf);
            free(tmpConfigRBuf);
            
        } else {
            
            /*
             * malloc succeeded; free the old buffers and use the new
             * buffers
             */
            
            configBufLen = CONFIG_BUF_LEN;
            
            free(configBuf);
            free(configRBuf);
            
            configBuf = tmpConfigBuf;
            configRBuf = tmpConfigRBuf;
        }
    }

    /* read in another block of chars */
    
    do {
        ret = fgets(configBuf + pos, configBufLen - pos - 1, configFile);
        
        if (!ret) break;
        
        /* search for EOL in the new block of chars */
        
        for (i = pos; i < (configBufLen - 1); i++) {
            c = configBuf[i];
            
            if (c == '\0') break;
            
            if ((c == '\n') || (c == '\r')) {
                eolFound = 1;
                break;
            }
        }
        
        /*
         * if we didn't find EOL, then grow the string and
         * read in more
         */
        
        if (!eolFound) {
            
            tmpConfigBuf = realloc(configBuf, configBufLen + CONFIG_BUF_LEN);
            tmpConfigRBuf = realloc(configRBuf, configBufLen + CONFIG_BUF_LEN);
            
            if (!tmpConfigBuf || !tmpConfigRBuf) {
                
                /*
                 * at least one of the reallocations failed; use the
                 * new allocation that succeeded, but we have to
                 * fallback to the previous configBufLen size and use
                 * the string we have, even though we don't have an
                 * EOL
                 */
                
                if (tmpConfigBuf) configBuf = tmpConfigBuf;
                if (tmpConfigRBuf) configRBuf = tmpConfigRBuf;
                
                break;
                
            } else {
                
                /* reallocation succeeded */

                configBuf = tmpConfigBuf;
                configRBuf = tmpConfigRBuf;
                pos = i;
                configBufLen += CONFIG_BUF_LEN;
            }
        }
        
    } while (!eolFound);
    
    return ret;
}



/* 
 * xconfigGetToken --
 *      Read next Token from the config file. Handle the global variable
 *      pushToken.
 */

int xconfigGetToken (XConfigSymTabRec * tab)
{
    int c, i;

    /* 
     * First check whether pushToken has a different value than LOCK_TOKEN.
     * In this case rBuf[] contains a valid STRING/TOKEN/NUMBER. But in the
     * oth * case the next token must be read from the input.
     */
    if (pushToken == EOF_TOKEN)
        return (EOF_TOKEN);
    else if (pushToken == LOCK_TOKEN)
    {
        /*
         * eol_seen is only set for the first token after a newline.
         */
        eol_seen = 0;

        c = configBuf[configPos];

        /* 
         * Get start of next Token. EOF is handled,
         * whitespaces are skipped. 
         */

again:
        if (!c)
        {
            char *ret;
            if (configFile)
                ret = xconfigGetNextLine();
            else {
                if (builtinConfig[builtinIndex] == NULL)
                    ret = NULL;
                else {
                    ret = strncpy(configBuf, builtinConfig[builtinIndex],
                            CONFIG_BUF_LEN);
                    builtinIndex++;
                }
            }
            if (ret == NULL)
            {
                return (pushToken = EOF_TOKEN);
            }
            configLineNo++;
            configPos = 0;
            eol_seen = 1;
        }

        i = 0;
        for (;;) {
            c = configBuf[configPos++];
            configRBuf[i++] = c;
            switch (c) {
                case ' ':
                case '\t':
                case '\r':
                    continue;
                case '\n':
                    i = 0;
                    continue;
            }
            break;
        }
        if (c == '\0')
            goto again;

        if (c == '#')
        {
            do
            {
                configRBuf[i++] = (c = configBuf[configPos++]);
            }
            while ((c != '\n') && (c != '\r') && (c != '\0'));
            configRBuf[i] = '\0';
            /* XXX no private copy.
             * Use xconfigAddComment when setting a comment.
             */
            val.str = configRBuf;
            return (COMMENT);
        }

        /* GJA -- handle '-' and ','  * Be careful: "-hsync" is a keyword. */
        else if ((c == ',') && !xconfigIsAlpha(configBuf[configPos]))
        {
            return COMMA;
        }
        else if ((c == '-') && !xconfigIsAlpha(configBuf[configPos]))
        {
            return DASH;
        }

        /* 
         * Numbers are returned immediately ...
         */
        if (xconfigIsDigit(c))
        {
            int base;

            if (c == '0')
                if ((configBuf[configPos] == 'x') ||
                    (configBuf[configPos] == 'X'))
                    base = 16;
                else
                    base = 8;
            else
                base = 10;

            configRBuf[0] = c;
            i = 1;
            while (xconfigIsDigit(c = configBuf[configPos++]) ||
                   (c == '.') || (c == 'x') || (c == 'X') ||
                   ((base == 16) && (((c >= 'a') && (c <= 'f')) ||
                                     ((c >= 'A') && (c <= 'F')))))
                configRBuf[i++] = c;
            configPos--;        /* GJA -- one too far */
            configRBuf[i] = '\0';
            val.num = xconfigStrToUL (configRBuf);
            val.realnum = atof (configRBuf);
            val.str = configRBuf;
            return (NUMBER);
        }

        /* 
         * All Strings START with a \" ...
         */
        else if (c == '\"')
        {
            i = -1;
            do
            {
                configRBuf[++i] = (c = configBuf[configPos++]);
            }
            while ((c != '\"') && (c != '\n') && (c != '\r') && (c != '\0'));
            configRBuf[i] = '\0';
            val.str = malloc (strlen (configRBuf) + 1);
            strcpy (val.str, configRBuf);    /* private copy ! */
            return (STRING);
        }

        /* 
         * ... and now we MUST have a valid token.  The search is
         * handled later along with the pushed tokens.
         */
        else
        {
            configRBuf[0] = c;
            i = 0;
            do
            {
                configRBuf[++i] = (c = configBuf[configPos++]);;
            }
            while ((c != ' ')  &&
                   (c != '\t') &&
                   (c != '\n') &&
                   (c != '\r') &&
                   (c != '\0') &&
                   (c != '#'));
            
            --configPos;
            configRBuf[i] = '\0';
            i = 0;
        }

    }
    else
    {

        /* 
         * Here we deal with pushed tokens. Reinitialize pushToken again. If
         * the pushed token was NUMBER || STRING return them again ...
         */
        int temp = pushToken;
        pushToken = LOCK_TOKEN;

        if (temp == COMMA || temp == DASH)
            return (temp);
        if (temp == NUMBER || temp == STRING)
            return (temp);
    }

    /* 
     * Joop, at last we have to lookup the token ...
     */
    if (tab)
    {
        i = 0;
        while (tab[i].token != -1)
            if (xconfigNameCompare (configRBuf, tab[i].name) == 0)
                return (tab[i].token);
            else
                i++;
    }

    return (ERROR_TOKEN);        /* Error catcher */
}

int xconfigGetSubToken (char **comment)
{
    int token;

    for (;;) {
        token = xconfigGetToken(NULL);
        if (token == COMMENT) {
            if (comment)
                *comment = xconfigAddComment(*comment, val.str);
        }
        else
            return (token);
    }
    /*NOTREACHED*/
}

int xconfigGetSubTokenWithTab (char **comment, XConfigSymTabRec *tab)
{
    int token;

    for (;;) {
        token = xconfigGetToken(tab);
        if (token == COMMENT) {
            if (comment)
                *comment = xconfigAddComment(*comment, val.str);
        }
        else
            return (token);
    }
    /*NOTREACHED*/
}

void xconfigUnGetToken (int token)
{
    pushToken = token;
}

char *xconfigTokenString (void)
{
    return configRBuf;
}

static int pathIsAbsolute(const char *path)
{
    if (path && path[0] == '/')
        return 1;
    return 0;
}

/* A path is "safe" if it is relative and if it contains no ".." elements. */
static int pathIsSafe(const char *path)
{
    if (pathIsAbsolute(path))
        return 0;

    /* Compare with ".." */
    if (!strcmp(path, ".."))
        return 0;

    /* Look for leading "../" */
    if (!strncmp(path, "../", 3))
        return 0;

    /* Look for trailing "/.." */
    if ((strlen(path) > 3) && !strcmp(path + strlen(path) - 3, "/.."))
        return 0;

    /* Look for "/../" */
    if (strstr(path, "/../"))
        return 0;

    return 1;
}

/*
 * This function substitutes the following escape sequences:
 *
 *    %A    cmdline argument as an absolute path (must be absolute to match)
 *    %R    cmdline argument as a relative path
 *    %S    cmdline argument as a "safe" path (relative, and no ".." elements)
 *    %X    default config file name ("XF86Config")
 *    %H    hostname
 *    %E    config file environment ($XF86CONFIG) as an absolute path
 *    %F    config file environment ($XF86CONFIG) as a relative path
 *    %G    config file environment ($XF86CONFIG) as a safe path
 *    %P    projroot
 *    %M    major version number
 *    %%    %
 *    %&    UNIXOS2 only: prepend X11ROOT env var
 */

#ifndef XCONFIGFILE
#define XCONFIGFILE    "xorg.conf"
#endif
#ifndef PROJECTROOT
#define PROJECTROOT    "/usr/X11R6"
#endif
#ifndef XCONFENV
#define XCONFENV    "XF86CONFIG"
#endif
#define XFREE86CFGFILE "XF86Config"
#ifndef X_VERSION_MAJOR
#ifdef XVERSION
#if XVERSION > 40000000
#define X_VERSION_MAJOR    (XVERSION / 10000000)
#else
#define X_VERSION_MAJOR    (XVERSION / 1000)
#endif
#else
#define X_VERSION_MAJOR    4
#endif
#endif

#define BAIL_OUT        do {                                \
                            free(result);                   \
                            return NULL;                    \
                        } while (0)

#define CHECK_LENGTH    do {                                \
                            if (l > PATH_MAX) {             \
                                BAIL_OUT;                   \
                            }                               \
                        } while (0)

#define APPEND_STR(s)    do {                               \
                            if (strlen(s) + l > PATH_MAX) { \
                                BAIL_OUT;                   \
                            } else {                        \
                                strcpy(result + l, s);      \
                                l += strlen(s);             \
                            }                               \
                        } while (0)

static char *DoSubstitution(const char *template,
                            const char *cmdline,
                            const char *projroot,
                            int *cmdlineUsed, int *envUsed, char *XConfigFile)
{
    char *result;
    int i, l;
    static const char *env = NULL;
    static char *hostname = NULL;
    static char majorvers[3] = "";

    if (!template)
        return NULL;

    if (cmdlineUsed)
        *cmdlineUsed = 0;
    if (envUsed)
        *envUsed = 0;

    result = malloc(PATH_MAX + 1);
    l = 0;
    for (i = 0; template[i]; i++) {
        if (template[i] != '%') {
            result[l++] = template[i];
            CHECK_LENGTH;
        } else {
            switch (template[++i]) {
            case 'A':
                if (cmdline && pathIsAbsolute(cmdline)) {
                    APPEND_STR(cmdline);
                    if (cmdlineUsed)
                        *cmdlineUsed = 1;
                } else
                    BAIL_OUT;
                break;
            case 'R':
                if (cmdline && !pathIsAbsolute(cmdline)) {
                    APPEND_STR(cmdline);
                    if (cmdlineUsed)
                        *cmdlineUsed = 1;
                } else 
                    BAIL_OUT;
                break;
            case 'S':
                if (cmdline && pathIsSafe(cmdline)) {
                    APPEND_STR(cmdline);
                    if (cmdlineUsed)
                        *cmdlineUsed = 1;
                } else 
                    BAIL_OUT;
                break;
            case 'X':
                APPEND_STR(XConfigFile);
                break;
            case 'H':
                if (!hostname) {
                    if ((hostname = malloc(MAXHOSTNAMELEN + 1))) {
                        if (gethostname(hostname, MAXHOSTNAMELEN) == 0) {
                            hostname[MAXHOSTNAMELEN] = '\0';
                        } else {
                            free(hostname);
                            hostname = NULL;
                        }
                    }
                }
                if (hostname)
                    APPEND_STR(hostname);
                break;
            case 'E':
                if (!env)
                    env = getenv(XCONFENV);
                if (env && pathIsAbsolute(env)) {
                    APPEND_STR(env);
                    if (envUsed)
                        *envUsed = 1;
                } else
                    BAIL_OUT;
                break;
            case 'F':
                if (!env)
                    env = getenv(XCONFENV);
                if (env && !pathIsAbsolute(env)) {
                    APPEND_STR(env);
                    if (envUsed)
                        *envUsed = 1;
                } else
                    BAIL_OUT;
                break;
            case 'G':
                if (!env)
                    env = getenv(XCONFENV);
                if (env && pathIsSafe(env)) {
                    APPEND_STR(env);
                    if (envUsed)
                        *envUsed = 1;
                } else
                    BAIL_OUT;
                break;
            case 'P':
                if (projroot && pathIsAbsolute(projroot))
                    APPEND_STR(projroot);
                else
                    BAIL_OUT;
                break;
            case 'M':
                if (!majorvers[0]) {
                    sprintf(majorvers, "%d", X_VERSION_MAJOR);
                }
                APPEND_STR(majorvers);
                break;
            case '%':
                result[l++] = '%';
                CHECK_LENGTH;
                break;
            default:
                xconfigErrorMsg(InternalErrorMsg,
                             "invalid escape %%%c found in path template\n",
                             template[i]);
                BAIL_OUT;
                break;
            }
        }
    }
    return result;
}

/* 
 * xconfigOpenConfigFile --
 *
 * This function takes a config file search path (optional), a
 * command-line specified file name (optional) and the ProjectRoot
 * path (optional) and locates and opens a config file based on that
 * information.  If a command-line file name is specified, then this
 * function fails if none of the located files.
 *
 * The return value is a pointer to the actual name of the file that
 * was opened.  When no file is found, the return value is NULL.
 *
 * The escape sequences allowed in the search path are defined above.
 *  
 */


/*
 * __root_configpath[] - this is the XconfigConfig search path used by
 * XFree86 when the server runs as root.
 */

static const char __root_configpath[] =
"%A,"               /* <cmdline> */
"%R,"               /* <cmdline> (as relative path) */
"/etc/X11/%R,"      /* /etc/X11/<cmdline> */
"%P/etc/X11/%R,"    /* /usr/X11R6/etc/X11/<cmdline> */
"%E,"               /* $XF86CONFIG */
"%F,"               /* $XF86CONFIG (as relative path) */
"/etc/X11/%F,"      /* /etc/X11/$XF86CONFIG */
"%P/etc/X11/%F,"    /* /usr/X11R6/etc/X11/$XF86CONFIG */
"/etc/X11/%X-%M,"   /* /etc/X11/XF86Config-4 */
"/etc/X11/%X,"      /* /etc/X11/XF86Config */
"/etc/%X,"          /* /etc/XF86Config */
"%P/etc/X11/%X.%H," /* /usr/X11R6/etc/X11/XF86Config.<hostname> */
"%P/etc/X11/%X-%M," /* /usr/X11R6/etc/X11/XF86Config-4 */
"%P/etc/X11/%X,"    /* /usr/X11R6/etc/X11/XF86Config */
"%P/lib/X11/%X.%H," /* /usr/X11R6/lib/X11/XF86Config.<hostname> */
"%P/lib/X11/%X-%M," /* /usr/X11R6/lib/X11/XF86Config-4 */
"%P/lib/X11/%X";    /* /usr/X11R6/lib/X11/XF86Config */



/*
 * __user_configpath[] - this is the XF86Config search path used by
 * XFree86 when the server runs as a normal user
 */

static const char __user_configpath[] =
"%A,"               /* <cmdline> XXX */
"%R,"               /* <cmdline> (as relative path) XXX */
"/etc/X11/%S,"      /* /etc/X11/<cmdline> */
"%P/etc/X11/%S,"    /* /usr/X11R6/etc/X11/<cmdline> */
"/etc/X11/%G,"      /* /etc/X11/$XF86CONFIG */
"%P/etc/X11/%G,"    /* /usr/X11R6/etc/X11/$XF86CONFIG */
"/etc/X11/%X-%M,"   /* /etc/X11/XF86Config-4 */
"/etc/X11/%X,"      /* /etc/X11/XF86Config */
"/etc/%X,"          /* /etc/XF86Config */
"%P/etc/X11/%X.%H," /* /usr/X11R6/etc/X11/XF86Config.<hostname> */
"%P/etc/X11/%X-%M," /* /usr/X11R6/etc/X11/XF86Config-4 */
"%P/etc/X11/%X,"    /* /usr/X11R6/etc/X11/XF86Config */
"%P/lib/X11/%X.%H," /* /usr/X11R6/lib/X11/XF86Config.<hostname> */
"%P/lib/X11/%X-%M," /* /usr/X11R6/lib/X11/XF86Config-4 */
"%P/lib/X11/%X";    /* /usr/X11R6/lib/X11/XF86Config */



const char *xconfigOpenConfigFile(const char *cmdline, const char *projroot)
{
    const char *searchpath;
    char *pathcopy;
    const char *template;
    int cmdlineUsed = 0;

    configFile = NULL;
    configPos = 0;        /* current readers position */
    configLineNo = 0;    /* linenumber */
    pushToken = LOCK_TOKEN;

    /*
     * select the search path: XFree86 uses a slightly different path
     * depending on whether the user is root
     */

    if (getuid() == 0) {
        searchpath = __root_configpath;
    } else {
        searchpath = __user_configpath;
    }

    if (!projroot) projroot = PROJECTROOT;
    
    pathcopy = strdup(searchpath);
    
    template = strtok(pathcopy, ",");

    /* First, search for a config file. */
    while (template && !configFile) {
        if ((configPath = DoSubstitution(template, cmdline, projroot,
                                         &cmdlineUsed, NULL, XCONFIGFILE))) {
            if ((configFile = fopen(configPath, "r")) != 0) {
                if (cmdline && !cmdlineUsed) {
                    fclose(configFile);
                    configFile = NULL;
                }
            }
        }
        if (configPath && !configFile) {
            free(configPath);
            configPath = NULL;
        }
        template = strtok(NULL, ",");
    }

    /* Then search for fallback */
    if (!configFile) {
        strcpy(pathcopy, searchpath);
        template = strtok(pathcopy, ",");
        
        while (template && !configFile) {
            if ((configPath = DoSubstitution(template, cmdline, projroot,
                                             &cmdlineUsed, NULL,
                                             XFREE86CFGFILE))) {
                if ((configFile = fopen(configPath, "r")) != 0) {
                    if (cmdline && !cmdlineUsed) {
                        fclose(configFile);
                        configFile = NULL;
                    }
                }
            }
            if (configPath && !configFile) {
                free(configPath);
                configPath = NULL;
            }
            template = strtok(NULL, ",");
        }
    }
    
    free(pathcopy);

    if (!configFile) {
        return NULL;
    }

    configBuf = malloc(CONFIG_BUF_LEN);
    configRBuf = malloc(CONFIG_BUF_LEN);
    configBuf[0] = '\0';

    return configPath;
}

void xconfigCloseConfigFile (void)
{
    free (configPath);
    configPath = NULL;
    free (configRBuf);
    configRBuf = NULL;
    free (configBuf);
    configBuf = NULL;

    if (configFile) {
        fclose (configFile);
        configFile = NULL;
    } else {
        builtinConfig = NULL;
        builtinIndex = 0;
    }
}


char *xconfigGetConfigFileName(void)
{
    return configPath;
}


void
xconfigSetSection (char *section)
{
    if (configSection)
        free(configSection);
    configSection = malloc(strlen (section) + 1);
    strcpy (configSection, section);
}

/* 
 * xconfigGetToken --
 *  Lookup a string if it is actually a token in disguise.
 */


char *
xconfigAddComment(char *cur, char *add)
{
    char *str;
    int len, curlen, iscomment, hasnewline = 0, endnewline;

    if (add == NULL || add[0] == '\0')
        return (cur);

    if (cur) {
        curlen = strlen(cur);
        if (curlen)
            hasnewline = cur[curlen - 1] == '\n';
        eol_seen = 0;
    }
    else
        curlen = 0;

    str = add;
    iscomment = 0;
    while (*str) {
        if (*str != ' ' && *str != '\t')
        break;
        ++str;
    }
    iscomment = (*str == '#');

    len = strlen(add);
    endnewline = add[len - 1] == '\n';
    len +=  1 + iscomment + (!hasnewline) + (!endnewline) + eol_seen;

    if ((str = realloc(cur, len + curlen)) == NULL)
        return (cur);

    cur = str;

    if (eol_seen || (curlen && !hasnewline))
        cur[curlen++] = '\n';
    if (!iscomment)
        cur[curlen++] = '#';
    strcpy(cur + curlen, add);
    if (!endnewline)
        strcat(cur, "\n");

    return (cur);
}

int
xconfigGetStringToken (XConfigSymTabRec * tab)
{
    return StringToToken (val.str, tab);
}

static int
StringToToken (char *str, XConfigSymTabRec * tab)
{
    int i;

    for (i = 0; tab[i].token != -1; i++)
    {
        if (!xconfigNameCompare (tab[i].name, str))
            return tab[i].token;
    }
    return (ERROR_TOKEN);
}


/* 
 * Compare two names.  The characters '_', ' ', and '\t' are ignored
 * in the comparison.
 */
int
xconfigNameCompare (const char *s1, const char *s2)
{
    char c1, c2;

    if (!s1 || *s1 == 0) {
        if (!s2 || *s2 == 0)
            return (0);
        else
            return (1);
        }

    while (*s1 == '_' || *s1 == ' ' || *s1 == '\t')
        s1++;
    while (*s2 == '_' || *s2 == ' ' || *s2 == '\t')
        s2++;
    c1 = (xconfigIsUpper(*s1) ? xconfigToLower(*s1) : *s1);
    c2 = (xconfigIsUpper(*s2) ? xconfigToLower(*s2) : *s2);
    while (c1 == c2)
    {
        if (c1 == '\0')
            return (0);
        s1++;
        s2++;
        while (*s1 == '_' || *s1 == ' ' || *s1 == '\t')
            s1++;
        while (*s2 == '_' || *s2 == ' ' || *s2 == '\t')
            s2++;
        c1 = (xconfigIsUpper(*s1) ? xconfigToLower(*s1) : *s1);
        c2 = (xconfigIsUpper(*s2) ? xconfigToLower(*s2) : *s2);
    }
    return (c1 - c2);
}

/* 
 * Compare two modelines.  The modeline identifiers and comments are
 * ignored in the comparison.
 */
int
xconfigModelineCompare(XConfigModeLinePtr m1, XConfigModeLinePtr m2)
{
    if (!m1 && !m2)
        return (0);

    if (!m1 || !m2)
        return (1);

    if (m1->clock      != m2->clock &&
        m1->hdisplay   != m2->hdisplay &&
        m1->hsyncstart != m2->hsyncstart &&
        m1->hsyncend   != m2->hsyncend &&
        m1->htotal     != m2->htotal &&
        m1->vdisplay   != m2->vdisplay &&
        m1->vsyncstart != m2->vsyncstart &&
        m1->vsyncend   != m2->vsyncend &&
        m1->vtotal     != m2->vtotal &&
        m1->vscan      != m2->vscan &&
        m1->flags      != m2->flags &&
        m1->hskew      != m2->hskew)
        return (1);
    return (0);
}
