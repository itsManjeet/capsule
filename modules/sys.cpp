#include <Interpreter.h>
using namespace SrcLang;

#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/ioctl.h>
#    include <sys/stat.h>
#    include <termios.h>
#    include <unistd.h>
#endif
#include <dirent.h>

#ifndef PATH_MAX
#    define PATH_MAX 8124
#endif

#define SYSERROR SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)))
#define SAFE_SYSCALL(call)                                                     \
    if ((call) < 0) return SYSERROR;

#define SAFE_RETURN(call) SAFE_SYSCALL(call) return SRCLANG_VALUE_TRUE;

SRCLANG_MODULE_FUNC(Environ) {
    SRCLANG_CHECK_ARGS_EXACT(0);
    auto env = new SrcLangList();
    for (char** e = environ; e != nullptr; e++) {
        env->push_back(SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_STRING(e)));
    }
    return SRCLANG_VALUE_LIST(env);
}

SRCLANG_MODULE_FUNC(Errno) {
    SRCLANG_CHECK_ARGS_EXACT(0);
    return SRCLANG_VALUE_NUMBER(errno);
}

SRCLANG_MODULE_FUNC(Exec) {
    SRCLANG_CHECK_ARGS_EXACT(2);

    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    auto cmd = reinterpret_cast<const char*>(
            SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);

    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Closure);

    char buffer[128] = {};
    FILE* fd = popen(cmd, "r");
    if (fd == nullptr) {
        return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)));
    }
    while (fgets(buffer, sizeof(buffer), fd) != nullptr) {
        interpreter->call(
                args[1], {SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_STRING(buffer))});
    }
    int status = pclose(fd);
    status = WEXITSTATUS(status);
    return SRCLANG_VALUE_NUMBER(status);
}

SRCLANG_MODULE_FUNC(Chdir) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    auto ch8str = wchtoch(SRCLANG_VALUE_AS_STRING(args[0]));
    int status = chdir(ch8str);
    free(ch8str);
    SAFE_RETURN(status);
}

SRCLANG_MODULE_FUNC(Dup) {
    SRCLANG_CHECK_ARGS_RANGE(1, 2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);
    int result;
    if (args.size() == 1) {
        result = dup(SRCLANG_VALUE_AS_NUMBER(args[0]));
    } else {
        result = dup2(SRCLANG_VALUE_AS_NUMBER(args[0]),
                SRCLANG_VALUE_AS_NUMBER(args[1]));
    }
    SAFE_SYSCALL(result);
    return SRCLANG_VALUE_NUMBER(result);
}

SRCLANG_MODULE_FUNC(Exit) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);

    auto status = (int)SRCLANG_VALUE_AS_NUMBER(args[0]);
    interpreter->graceFullExit();
    exit(status);
}

SRCLANG_MODULE_FUNC(Getcwd) {
    SRCLANG_CHECK_ARGS_EXACT(0);
    char p[PATH_MAX];
    if ((getcwd(p, sizeof(p))) == nullptr) return SYSERROR;
    return SRCLANG_VALUE_STRING(strdup(p));
}

SRCLANG_MODULE_FUNC(Getenv) {
    SRCLANG_CHECK_ARGS_RANGE(1, 2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    auto ch8str = wchtoch(SRCLANG_VALUE_AS_STRING(args[0]));
    char* value = getenv(ch8str);
    free(ch8str);
    if (value == nullptr) return SRCLANG_VALUE_STRING(strdup(value));
    return args.size() == 2 ? args[1] : SRCLANG_VALUE_NULL;
}

#define DIRECT_METHOD(id, fun)                                                 \
    SRCLANG_MODULE_FUNC(id) {                                                  \
        SRCLANG_CHECK_ARGS_EXACT(0);                                           \
        int value = fun();                                                     \
        if (value == -1) return SYSERROR;                                      \
        return SRCLANG_VALUE_NUMBER(value);                                    \
    }

DIRECT_METHOD(Getegid, getegid)
DIRECT_METHOD(Geteuid, geteuid)

SRCLANG_MODULE_FUNC(Getkey) {
    SRCLANG_CHECK_ARGS_EXACT(0);
    termios raw, original;

    SAFE_SYSCALL(tcgetattr(STDIN_FILENO, &original));

    raw = original;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    SAFE_SYSCALL(tcsetattr(STDIN_FILENO, TCSANOW, &raw));

    char buf[6];
    auto const count = read(STDIN_FILENO, buf, 6);
    tcsetattr(STDIN_FILENO, TCSANOW, &original);
    if (count == -1) return SYSERROR;
    buf[count] = '\0';

    return SRCLANG_VALUE_STRING(chtowch(buf));
}

DIRECT_METHOD(Getgid, getgid)

SRCLANG_MODULE_FUNC(Getgroups) {
    SRCLANG_CHECK_ARGS_EXACT(0);

    int maxgroups = sysconf(_SC_NGROUPS_MAX);
    gid_t groups[maxgroups];
    int ngroups = getgroups(maxgroups, groups);
    SAFE_SYSCALL(ngroups);

    auto groups_ = new SrcLangList(ngroups);
    for (int i = 0; i < ngroups; i++) {
        groups_->push_back(SRCLANG_VALUE_NUMBER(groups[i]));
    }
    return SRCLANG_VALUE_LIST(groups_);
}

SRCLANG_MODULE_FUNC(Getpgid) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);

    auto pgid = getpgid(SRCLANG_VALUE_AS_NUMBER(args[0]));
    SAFE_SYSCALL(pgid);

    return SRCLANG_VALUE_NUMBER(pgid);
}

DIRECT_METHOD(Getpgrp, getpgrp)
DIRECT_METHOD(Getpid, getpid)
DIRECT_METHOD(Getuid, getuid)

SRCLANG_MODULE_FUNC(Gettermsize) {
    SRCLANG_CHECK_ARGS_RANGE(0, 1);
    int fd = STDOUT_FILENO;
    if (args.size() == 1) {
        SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);
        fd = SRCLANG_VALUE_AS_NUMBER(args[0]);
    }

    winsize size;
    SAFE_SYSCALL(ioctl(fd, TIOCGWINSZ, &size));

    auto geo = new SrcLangMap();
    (*geo)[L"Columns"] = SRCLANG_VALUE_NUMBER(size.ws_col);
    (*geo)[L"Rows"] = SRCLANG_VALUE_NUMBER(size.ws_row);

    return SRCLANG_VALUE_MAP(geo);
}

SRCLANG_MODULE_FUNC(Mkdir) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    std::filesystem::path path = SRCLANG_VALUE_AS_STRING(args[0]);

    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) { return SRCLANG_VALUE_ERROR(strdup(error.message().c_str())); }
    return SRCLANG_VALUE_TRUE;
}

SRCLANG_MODULE_FUNC(Opendir) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Closure);
    auto ch8str = wchtoch(SRCLANG_VALUE_AS_STRING(args[0]));
    auto dir = opendir(ch8str);
    free(ch8str);
    if (dir == nullptr) return SYSERROR;

    auto cmd_args = std::vector<Value>(2);
    cmd_args[1] = args[0];

    auto sdirent = new SrcLangMap();
    auto svalue = interpreter->addObject(SRCLANG_VALUE_MAP(sdirent));
    Value result = SRCLANG_VALUE_TRUE;

    for (dirent* iter; iter = readdir(dir);) {
        (*sdirent)[L"name"] =
                SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_STRING(iter->d_name));
        (*sdirent)[L"type"] = SRCLANG_VALUE_NUMBER(iter->d_type);
        (*sdirent)[L"inode"] = SRCLANG_VALUE_NUMBER(iter->d_ino);
        (*sdirent)[L"offset"] = SRCLANG_VALUE_NUMBER(iter->d_off);
        (*sdirent)[L"reclen"] = SRCLANG_VALUE_NUMBER(iter->d_reclen);

        cmd_args[0] = svalue;
        if (result = interpreter->call(args[1], cmd_args);
                SRCLANG_VALUE_IS_ERROR(result)) {
            break;
        }
    }

    closedir(dir);
    return result;
}

SRCLANG_MODULE_FUNC(Open) {
    SRCLANG_CHECK_ARGS_RANGE(1, 2);
    const char* mode = "a+";
    if (args.size() == 2) {
        SRCLANG_CHECK_ARGS_TYPE(1, ValueType::String);
        mode = reinterpret_cast<const char*>(
                SRCLANG_VALUE_AS_OBJECT(args[1])->pointer);
    }

    FILE* fp = nullptr;
    switch (SRCLANG_VALUE_GET_TYPE(args[0])) {
    case ValueType::Number:
        fp = fdopen(SRCLANG_VALUE_AS_NUMBER(args[0]), mode);
        break;
    case ValueType::String:
        fp = fopen(reinterpret_cast<const char*>(
                           SRCLANG_VALUE_AS_OBJECT(args[0])->pointer),
                mode);
        break;
    case ValueType::Pointer:
        fp = reinterpret_cast<FILE*>(SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);
        break;
    default: SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    }
    if (fp == nullptr) {
        return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)));
    }

    auto object = new SrcLangMap();
    auto value = SRCLANG_VALUE_MAP(object);

    object->insert({L"__ptr__",
            SRCLANG_VALUE_SET_CLEANUP(
                    SRCLANG_VALUE_POINTER(reinterpret_cast<void*>(fp)),
                    +[](void* ptr) {
                        auto fp = reinterpret_cast<FILE*>(ptr);
                        if (fp != nullptr) fclose(fp);
                    })});

#define ADD_METHOD(id, fun)                                                    \
    object->insert({s2ws(#id),                                                 \
            SRCLANG_VALUE_BOUND(value,                                         \
                    SRCLANG_VALUE_BUILTIN_NEW(+[](std::vector<Value>& args)    \
                                                      -> Value {               \
                        SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Map);            \
                        auto self = reinterpret_cast<SrcLangMap*>(             \
                                SRCLANG_VALUE_AS_OBJECT(args[0])->pointer);    \
                        auto file = reinterpret_cast<FILE*>(                   \
                                SRCLANG_VALUE_AS_OBJECT(self->at(L"__ptr__"))  \
                                        ->pointer);                            \
                        fun                                                    \
                    }))});

    ADD_METHOD(Write, {
        SRCLANG_CHECK_ARGS_EXACT(2);
        SRCLANG_CHECK_ARGS_TYPE(1, ValueType::String);
        auto data = SRCLANG_VALUE_AS_STRING(args[1]);
        int num = fwrite(data, sizeof(wchar_t), wchlen(data), file);
        if (num == -1) {
            return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)));
        }
        return SRCLANG_VALUE_NUMBER(num);
    })

    ADD_METHOD(Seek, {
        SRCLANG_CHECK_ARGS_EXACT(3);
        SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Number);
        SRCLANG_CHECK_ARGS_TYPE(2, ValueType::Number);

        int offset = SRCLANG_VALUE_AS_NUMBER(args[1]);
        int whence = SRCLANG_VALUE_AS_NUMBER(args[2]);

        if (fseek(file, offset, whence) != -1) {
            return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)));
        }
        return SRCLANG_VALUE_NUMBER(ftell(file));
    })

    ADD_METHOD(Chown, {
        SRCLANG_CHECK_ARGS_EXACT(3);
        SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Number);
        SRCLANG_CHECK_ARGS_TYPE(2, ValueType::Number);

        int uid = SRCLANG_VALUE_AS_NUMBER(args[1]);
        int gid = SRCLANG_VALUE_AS_NUMBER(args[2]);

        if (fchown(fileno(file), uid, gid) != -1) {
            return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)));
        }
        return SRCLANG_VALUE_TRUE;
    })

    ADD_METHOD(Datasync, {
        SRCLANG_CHECK_ARGS_EXACT(1);

        if (fdatasync(fileno(file)) != -1) {
            return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)));
        }
        return SRCLANG_VALUE_TRUE;
    })

    ADD_METHOD(Sync, {
        SRCLANG_CHECK_ARGS_EXACT(1);

        if (fsync(fileno(file)) != -1) {
            return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)));
        }
        return SRCLANG_VALUE_TRUE;
    })

    ADD_METHOD(Isatty, {
        SRCLANG_CHECK_ARGS_EXACT(1);
        return isatty(fileno(file)) == 1 ? SRCLANG_VALUE_TRUE
                                         : SRCLANG_VALUE_FALSE;
    })

    ADD_METHOD(Stat, {
        SRCLANG_CHECK_ARGS_EXACT(1);
        struct stat s;
        if (fstat(fileno(file), &s) == -1) {
            return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)));
        }
        auto status = new SrcLangMap();
        (*status)[L"Device"] = SRCLANG_VALUE_NUMBER(s.st_dev);
        (*status)[L"Inode"] = SRCLANG_VALUE_NUMBER(s.st_ino);
        (*status)[L"Links"] = SRCLANG_VALUE_NUMBER(s.st_nlink);
        (*status)[L"Mode"] = SRCLANG_VALUE_NUMBER(s.st_mode);
        (*status)[L"Uid"] = SRCLANG_VALUE_NUMBER(s.st_uid);
        (*status)[L"Gid"] = SRCLANG_VALUE_NUMBER(s.st_gid);
        (*status)[L"Rdev"] = SRCLANG_VALUE_NUMBER(s.st_rdev);
        (*status)[L"Size"] = SRCLANG_VALUE_NUMBER(s.st_size);
        (*status)[L"Blocksize"] = SRCLANG_VALUE_NUMBER(s.st_blksize);
        (*status)[L"Blocks"] = SRCLANG_VALUE_NUMBER(s.st_blocks);
        return SRCLANG_VALUE_MAP(status);
    })

    ADD_METHOD(Close, {
        SRCLANG_CHECK_ARGS_EXACT(1);

        if (fclose(file) != -1) {
            return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)));
        }
        return SRCLANG_VALUE_TRUE;
    })

    ADD_METHOD(Read, {
        SRCLANG_CHECK_ARGS_EXACT(2);
        SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Number);

        int size = SRCLANG_VALUE_AS_NUMBER(args[1]);
        auto buffer = new char[size + 1];

        int num = fread(buffer, sizeof(char), size, file);
        if (num == -1) {
            delete[] buffer;
            return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_ERROR(strerror(errno)));
        }
        buffer[size] = '\0';
        return SRCLANG_VALUE_STRING(buffer);
    })

    return value;
}

SRCLANG_MODULE_FUNC(Putenv) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    SAFE_RETURN(putenv((char*)SRCLANG_VALUE_AS_STRING(args[0])));
}

SRCLANG_MODULE_FUNC(Random) {
    SRCLANG_CHECK_ARGS_RANGE(0, 2);
    if (args.size() == 1) {
        return SRCLANG_VALUE_NUMBER(
                (rand() % (long)SRCLANG_VALUE_AS_NUMBER(args[0])));
    } else if (args.size() == 2) {
        return SRCLANG_VALUE_NUMBER(
                (rand() % (long)SRCLANG_VALUE_AS_NUMBER(args[1])) +
                SRCLANG_VALUE_AS_NUMBER(args[0]));
    } else {
        return SRCLANG_VALUE_NUMBER(rand());
    }
}

#define SET_METHOD(id, fun)                                                    \
    SRCLANG_MODULE_FUNC(id) {                                                  \
        SRCLANG_CHECK_ARGS_EXACT(1);                                           \
        SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);                         \
        SAFE_RETURN(fun(SRCLANG_VALUE_AS_NUMBER(args[0])));                    \
    }

SET_METHOD(Setegid, setegid)
SET_METHOD(Seteuid, seteuid)
SET_METHOD(Setgid, setgid)

SRCLANG_MODULE_FUNC(Setns) {
    SRCLANG_CHECK_ARGS_EXACT(2);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);
    SRCLANG_CHECK_ARGS_TYPE(1, ValueType::Number);

    SAFE_RETURN(setns(
            SRCLANG_VALUE_AS_NUMBER(args[0]), SRCLANG_VALUE_AS_NUMBER(args[1])))
}

SRCLANG_MODULE_FUNC(Strerror) {
    SRCLANG_CHECK_ARGS_RANGE(0, 1);
    if (args.size() == 1) SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    return SRCLANG_VALUE_SET_REF(SRCLANG_VALUE_STRING(strerror(
            args.size() == 1 ? SRCLANG_VALUE_AS_NUMBER(args[0]) : errno)));
}

SRCLANG_MODULE_FUNC(System) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    auto chstr = wchtoch(SRCLANG_VALUE_AS_STRING(args[0]));
    int status = WEXITSTATUS(system(chstr));
    free(chstr);
    return SRCLANG_VALUE_NUMBER(status);
}

SRCLANG_MODULE_FUNC(Unsetenv) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::String);
    auto chstr = wchtoch(SRCLANG_VALUE_AS_STRING(args[0]));
    int status = unsetenv(chstr);
    free(chstr);
    SAFE_RETURN(status);
}

SRCLANG_MODULE_FUNC(Unshare) {
    SRCLANG_CHECK_ARGS_EXACT(1);
    SRCLANG_CHECK_ARGS_TYPE(0, ValueType::Number);
    SAFE_RETURN(unshare(SRCLANG_VALUE_AS_NUMBER(args[0])));
}

SRCLANG_MODULE_INIT {
    SRCLANG_MODULE_DEFINE_CONST(STDIN_FILENO);
    SRCLANG_MODULE_DEFINE_CONST(STDOUT_FILENO);
    SRCLANG_MODULE_DEFINE_CONST(STDERR_FILENO);

    SRCLANG_MODULE_DEFINE_CONST(DT_UNKNOWN);
    SRCLANG_MODULE_DEFINE_CONST(DT_FIFO);
    SRCLANG_MODULE_DEFINE_CONST(DT_CHR);
    SRCLANG_MODULE_DEFINE_CONST(DT_DIR);
    SRCLANG_MODULE_DEFINE_CONST(DT_BLK);
    SRCLANG_MODULE_DEFINE_CONST(DT_REG);
    SRCLANG_MODULE_DEFINE_CONST(DT_LNK);
    SRCLANG_MODULE_DEFINE_CONST(DT_SOCK);
    SRCLANG_MODULE_DEFINE_CONST(DT_WHT);

    SRCLANG_MODULE_DEFINE_CONST(CLONE_FILES);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_FS);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_NEWCGROUP);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_NEWIPC);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_NEWNET);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_NEWNS);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_NEWPID);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_NEWUTS);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_NEWUSER);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_SIGHAND);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_SYSVSEM);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_THREAD);
    SRCLANG_MODULE_DEFINE_CONST(CLONE_VM);

    SRCLANG_MODULE_DEFINE_FUNC(Environ);
    SRCLANG_MODULE_DEFINE_FUNC(Errno);

    SRCLANG_MODULE_DEFINE_FUNC(Chdir);
    SRCLANG_MODULE_DEFINE_FUNC(Dup);
    SRCLANG_MODULE_DEFINE_FUNC(Exit);
    SRCLANG_MODULE_DEFINE_FUNC(Exec);

    SRCLANG_MODULE_DEFINE_FUNC(Getcwd);
    SRCLANG_MODULE_DEFINE_FUNC(Getenv);
    SRCLANG_MODULE_DEFINE_FUNC(Getegid);
    SRCLANG_MODULE_DEFINE_FUNC(Geteuid);
    SRCLANG_MODULE_DEFINE_FUNC(Getkey);
    SRCLANG_MODULE_DEFINE_FUNC(Getgid);
    SRCLANG_MODULE_DEFINE_FUNC(Getgroups);
    SRCLANG_MODULE_DEFINE_FUNC(Getpgid);
    SRCLANG_MODULE_DEFINE_FUNC(Getpgrp);
    SRCLANG_MODULE_DEFINE_FUNC(Getpid);
    SRCLANG_MODULE_DEFINE_FUNC(Getuid);
    SRCLANG_MODULE_DEFINE_FUNC(Gettermsize);

    SRCLANG_MODULE_DEFINE_FUNC(Mkdir);

    SRCLANG_MODULE_DEFINE_FUNC(Opendir);
    SRCLANG_MODULE_DEFINE_FUNC(Open);

    SRCLANG_MODULE_DEFINE_FUNC(Putenv);

    SRCLANG_MODULE_DEFINE_FUNC(Random);

    SRCLANG_MODULE_DEFINE_FUNC(Setegid);
    SRCLANG_MODULE_DEFINE_FUNC(Seteuid);
    SRCLANG_MODULE_DEFINE_FUNC(Setgid);
    SRCLANG_MODULE_DEFINE_FUNC(Setns);
    SRCLANG_MODULE_DEFINE_FUNC(Strerror);

    SRCLANG_MODULE_DEFINE_FUNC(System);
    SRCLANG_MODULE_DEFINE_FUNC(Unsetenv);
    SRCLANG_MODULE_DEFINE_FUNC(Unshare);
}