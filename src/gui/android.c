/* SPDX-License-Identifier: GPL-3.0-or-later
   Copyright 2026 the ScarletNX contributors */
#include "gui/android.h"
#include "gui/model.h"
#include "common/utilities/common.h"
#include "gui/i18n.h"
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void gui_android_start(void) {
    const char* files = SDL_GetAndroidInternalStoragePath();
    if (files) setenv("HOME", files, 1);
    if (files) chdir(files); /* then the data folder, gui_android_own_folder */
}

const char* gui_android_own_folder(void) {
    static char folder[1024];
    const char* files = SDL_GetAndroidInternalStoragePath();
    if (!files || snprintf(folder, sizeof folder, "%s/" SNX_PORTABLE_DIR, files) >= (int)sizeof folder) return NULL;
    mkdir(folder, 0770);
    if (access(folder, W_OK) || chdir(folder)) return NULL;
    mkdir("games", 0770);
    return folder;
}

char* gui_android_document_file(const char* uri) {
    SDL_IOStream* io = SDL_IOFromFile(uri, "rb");
    long fd =
        io ? (long)SDL_GetNumberProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_FILE_DESCRIPTOR_NUMBER, -1) : -1;
    char link[64], path[4096], *file;
    ssize_t n = -1;
    if (fd >= 0) {
        snprintf(link, sizeof link, "/proc/self/fd/%ld", fd);
        n = readlink(link, path, sizeof path - 1);
    }
    if (io) SDL_CloseIO(io);
    if (n <= 0 || path[0] != '/') return NULL;
    path[n] = 0;
    /* the provider may open it on the storage's own file system, which is the user's shared storage */
    if (!strncmp(path, "/data/media/", 12)) {
        file = (char*)malloc((size_t)n + 32);
        if (file) snprintf(file, (size_t)n + 32, "/storage/emulated/%s", path + 12);
    } else
        file = strdup(path);
    if (file && access(file, R_OK)) {
        free(file);
        file = NULL;
    }
    return file;
}

const char* gui_android_libraries(void) {
    static char folder[1024];
    Dl_info info;
    char* slash;
    if (!folder[0] && dladdr((void*)gui_android_libraries, &info) && info.dli_fname &&
        strlen(info.dli_fname) < sizeof folder) {
        strcpy(folder, info.dli_fname);
        slash = strrchr(folder, '/');
        if (slash) *slash = 0;
    }
    return folder[0] ? folder : NULL;
}

/* MainActivity's method, with its arguments (none for a "()V" one, which leaves them unread) */
static int call(const char* method, const char* signature, const jvalue* args, char* message, size_t size) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jobject activity = (jobject)SDL_GetAndroidActivity();
    jclass activity_class;
    jmethodID id;
    int ok;
    if (!env || !activity) {
        snprintf(message, size, "%s", snx_tr("No Android activity to ask."));
        return 0;
    }
    activity_class = (*env)->GetObjectClass(env, activity);
    id = (*env)->GetMethodID(env, activity_class, method, signature);
    if (id) (*env)->CallVoidMethodA(env, activity, id, args);
    ok = id && !(*env)->ExceptionCheck(env);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, activity_class);
    (*env)->DeleteLocalRef(env, activity);
    if (!ok) snprintf(message, size, snx_tr("MainActivity.%s did not run."), method);
    return ok;
}

int gui_android_play(const char* const* argv, char* message, size_t size) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jclass string;
    jobjectArray args;
    jvalue arg;
    int n = 0, i, ok;
    if (!env) return call("play", "", NULL, message, size);
    while (argv[n]) ++n;
    string = (*env)->FindClass(env, "java/lang/String");
    args = (*env)->NewObjectArray(env, n, string, NULL);
    for (i = 0; i < n; ++i) {
        jstring s = (*env)->NewStringUTF(env, argv[i]);
        (*env)->SetObjectArrayElement(env, args, i, s);
        (*env)->DeleteLocalRef(env, s);
    }
    arg.l = args;
    ok = call("play", "([Ljava/lang/String;)V", &arg, message, size);
    (*env)->DeleteLocalRef(env, args);
    (*env)->DeleteLocalRef(env, string);
    return ok;
}

int gui_android_share(const char* path, char* message, size_t size) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    jvalue arg;
    int ok;
    if (!env) return call("share", "", NULL, message, size);
    arg.l = (*env)->NewStringUTF(env, path);
    ok = call("share", "(Ljava/lang/String;)V", &arg, message, size);
    (*env)->DeleteLocalRef(env, arg.l);
    return ok;
}
int gui_android_background(char* message, size_t size) { return call("toBack", "()V", NULL, message, size); }
int gui_android_open_data(char* message, size_t size) { return call("openData", "()V", NULL, message, size); }

static SDL_DialogFileCallback folder_done;
static void* folder_data;
int gui_android_pick_folder(SDL_DialogFileCallback done, void* data, char* message, size_t size) {
    folder_done = done;
    folder_data = data;
    return call("pickFolder", "()V", NULL, message, size);
}
/* MainActivity's answer from the picker: the tree's URI as UTF-8, NULL for none picked */
JNIEXPORT void JNICALL Java_org_scarletnx_MainActivity_folderPicked(JNIEnv* env, jclass activity, jbyteArray uri);
JNIEXPORT void JNICALL Java_org_scarletnx_MainActivity_folderPicked(JNIEnv* env, jclass activity, jbyteArray uri) {
    const char* list[2] = {NULL, NULL};
    char *text = NULL, *path = NULL;
    jsize n = uri ? (*env)->GetArrayLength(env, uri) : 0;
    (void)activity;
    if (!folder_done) return;
    if (uri && (text = (char*)malloc((size_t)n + 1))) {
        (*env)->GetByteArrayRegion(env, uri, 0, n, (jbyte*)text);
        text[n] = 0;
        path = gui_document_path(text);
        list[0] = path ? path : text;
    }
    folder_done(folder_data, list, -1);
    free(path);
    free(text);
}

/* text as a byte[], which MainActivity reads as UTF-8: NewStringUTF aborts on anything else than modified UTF-8 */
static jbyteArray bytes(JNIEnv* env, const char* text) {
    jsize n = (jsize)strlen(text);
    jbyteArray array = (*env)->NewByteArray(env, n);
    if (array) (*env)->SetByteArrayRegion(env, array, 0, n, (const jbyte*)text);
    return array;
}

void gui_android_running(const char* game, const char* step, int percent) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    char message[64];
    jvalue args[3];
    if (!env) return;
    args[0].l = bytes(env, game);
    args[1].l = bytes(env, step);
    args[2].i = percent;
    if (args[0].l && args[1].l)
        call("running", "([B[BI)V", args, message, sizeof message);
    else
        (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, args[0].l);
    (*env)->DeleteLocalRef(env, args[1].l);
}

void gui_android_ran(const char* result) {
    JNIEnv* env = (JNIEnv*)SDL_GetAndroidJNIEnv();
    char message[64];
    jvalue arg;
    if (!env) return;
    arg.l = result ? bytes(env, result) : NULL;
    if (arg.l || !result)
        call("ran", "([B)V", &arg, message, sizeof message);
    else
        (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, arg.l);
}
