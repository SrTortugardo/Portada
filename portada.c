#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#define BUF 1024
#define CACHE_DIR_SUBPATH "/.config/imagecache" // aqui guardamos imagenes
#define IMG_SIZE 512
static volatile sig_atomic_t running = 1;
static char cache_dir[PATH_MAX] = {0};

static void handle_sig(int sig) {
    (void)sig;
    running = 0;
}

static void
run_capture_one(const char *cmd, char *buf, size_t bufsz) // Ejecutamos comando y escribimos la primera linea de salida en el buffer solo si el buffer no es null
{ 
    if (!cmd)
    {
        return;
    }

    FILE *fp = popen(cmd, "r");
    if (!fp)
    {
        if (buf && bufsz) buf[0] = '\0';
        return;
    }
    if (buf && bufsz) {
        if (fgets(buf, (int)bufsz, fp)) {
            buf[strcspn(buf, "\r\n")] = '\0';
        } else {
            buf[0] = '\0';
        }
    }
    pclose(fp);
}

/* Crea el directorio cache si no existe */
static int
ensure_cache_dir(void)
{
    const char *home = getenv("HOME"); // mi casa tio
    if (!home)
    {
        printf("No tienes casa chaval");
        return -1;
    }
    snprintf(cache_dir, sizeof(cache_dir), "%s%s", home, CACHE_DIR_SUBPATH);
    struct stat st;
    if (stat(cache_dir, &st) == 0)
    {
        if (S_ISDIR(st.st_mode)) return 0;
        else return -1;
    }
    // aqui creamos el directorio, no hay mucho mas
    char tmp[PATH_MAX];
    char *p;
    snprintf(tmp, sizeof(tmp), "%s", cache_dir);
    for (p = tmp + 1; *p; ++p)
    {
        if (*p == '/')
        {
            *p = '\0';
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0700) == -1 && errno != EEXIST)
    {
        return -1;
    }
    return 0;
}

static void
remove_dir_recursive(const char *dirpath) // me importa una guayaba y borrare el contenido del directorio RECURSIVAMENTE como el prota que soy(callenme porfa)
{
    if (!dirpath) return;
    DIR *d = opendir(dirpath);
    if (!d) return;
    struct dirent *ent;
    char path[PATH_MAX];
    while ((ent = readdir(d)) != NULL)
    {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        snprintf(path, sizeof(path), "%s/%s", dirpath, ent->d_name);
        struct stat st;
        if (lstat(path, &st) == -1) continue;
        if (S_ISDIR(st.st_mode)) {
            // RECURSIVO
            remove_dir_recursive(path);
            rmdir(path);
        } else {
            unlink(path);
        }
    }
    closedir(d);
}

static void
make_cache_names(char *out_raw, size_t rawsz, char *out_resized, size_t resizedsz, int counter)
{ // Creamos nombre unico y especial para la imagen en la carpeta CACHEte
    pid_t pid = getpid();
    snprintf(out_raw, rawsz, "%s/portada-%d-%d-raw.jpg", cache_dir, (int)pid, counter);
    snprintf(out_resized, resizedsz, "%s/portada-%d-%d.jpg", cache_dir, (int)pid, counter);
}

static int
file_exists(const char *path)  // Existe fichero SI o NO
{
    struct stat st;
    return (path && stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static int
run_cmd_wait(const char *cmd) // Se ejecuta comando de manera silenciosa, y esManzanaPera
{
    int rc = system(cmd);
    if (rc == -1) return -1;
    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return -1;
}

static int
extract_and_resize(const char *audio_path, const char *raw_path, const char *resized_path) // Extraemos portaada y redimenzionamos
{
    if (!audio_path || !raw_path || !resized_path)
    {
        return -1;
    }

    char cmd[PATH_MAX * 2];
    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -i \"%s\" -map 0:v:0 -c copy \"%s\" >/dev/null 2>&1",
        audio_path, raw_path);

    if (run_cmd_wait(cmd) != 0 || !file_exists(raw_path))
    {
        char dircpy[PATH_MAX];
        strncpy(dircpy, audio_path, sizeof(dircpy)-1); dircpy[sizeof(dircpy)-1]=0;
        char *d = dirname(dircpy);
        const char *cands[] = {"cover.jpg","cover.png","folder.jpg","front.jpg","art.jpg","cover.jpeg", NULL};
        for (int i = 0; cands[i]; ++i)
        {
            char sc[PATH_MAX];
            snprintf(sc, sizeof(sc), "%s/%s", d, cands[i]);
            if (file_exists(sc))
            {
                snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s\" >/dev/null 2>&1", sc, raw_path);
                if (run_cmd_wait(cmd) == 0 && file_exists(raw_path)) break;
            }
        }
        if (!file_exists(raw_path))
        {
            return -1;
        }
    }

    snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -i \"%s\" -vf \"scale=%d:%d:force_original_aspect_ratio=decrease,pad=%d:%d:(ow-iw)/2:(oh-ih)/2\" -format jpeg \"%s\" >/dev/null 2>&1",
        raw_path, IMG_SIZE, IMG_SIZE, IMG_SIZE, IMG_SIZE, resized_path);

    int rc = run_cmd_wait(cmd);
    if (rc != 0)
    {
        snprintf(cmd, sizeof(cmd),
            "ffmpeg -y -i \"%s\" -vf \"scale=%d:%d:force_original_aspect_ratio=decrease,pad=%d:%d:(ow-iw)/2:(oh-ih)/2\" \"%s\" >/dev/null 2>&1",
            raw_path, IMG_SIZE, IMG_SIZE, IMG_SIZE, IMG_SIZE, resized_path);
        rc = run_cmd_wait(cmd);
        if (rc != 0 || !file_exists(resized_path))
        {
            if (file_exists(raw_path)) unlink(raw_path);
            return -1;
        }
    }

    if (file_exists(raw_path)) unlink(raw_path);
    return 0;
}

void 
show_with_kitty(const char *path)
{
    char cmd[1024];

    system("kitty +kitten icat --clear");
    //snprintf(cmd, sizeof(cmd), "kitty +kitten icat --transfer-mode=file \"%s\"", path);
    snprintf(cmd, sizeof(cmd), "kitty icat --transfer-mode=memory --stdin=no \"%s\"", path);
    system(cmd);
}


int
main(void)
{
    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    if (ensure_cache_dir() != 0)
    {
        fprintf(stderr, "Error: no pude crear el directorio de cache en %s\n", cache_dir);
        return 1;
    }

    char prev_file[PATH_MAX] = {0};
    char cur_file[PATH_MAX] = {0};
    char title[BUF] = {0};
    char artist[BUF] = {0};
    int counter = 0;

    while (running)
    {
        run_capture_one("cmus-remote -Q | awk '/^file /{print substr($0,6); exit}'", cur_file, sizeof(cur_file));
        run_capture_one("cmus-remote -Q | awk '/^tag title /{print substr($0,11); exit}'", title, sizeof(title));
        run_capture_one("cmus-remote -Q | awk '/^tag artist /{print substr($0,12); exit}'", artist, sizeof(artist));

        if (cur_file[0] == '\0')
        {
            printf("\033[2J\033[H"); // clear
            printf("Creo que CMUS no corre o no hay ninguna cancion, prueba iniciarlo o poner una cancion\n");
            fflush(stdout);
            sleep(1);
            continue;
        }

        if (strcmp(cur_file, prev_file) != 0)
        {
            counter++;
            char raw_path[PATH_MAX], resized_path[PATH_MAX];
            make_cache_names(raw_path, sizeof(raw_path), resized_path, sizeof(resized_path), counter);

            int ok = extract_and_resize(cur_file, raw_path, resized_path);
            printf("\033[2J\033[H");
            printf(" %s\n", title[0] ? title : "(Desconocido)");
            printf("󰠃 %s\n\n", artist[0] ? artist : "(Desconocido)");
            fflush(stdout);

            if (ok == 0 && file_exists(resized_path))
            {
                show_with_kitty(resized_path);
            } else
            {
                printf("No se extrayo ni una sola portada cabeza de almendra\n");
                fflush(stdout);
            }

            strncpy(prev_file, cur_file, sizeof(prev_file)-1);
            prev_file[sizeof(prev_file)-1] = '\0';
        } else
            {
            printf("\033[H");
            printf(" %s\n", title[0] ? title : "(Desconocido)");
            printf("󰠃 %s\n\n", artist[0] ? artist : "(Desconocido)");
            fflush(stdout);
            }

        struct timespec ts;
        ts.tv_sec = 0;
        ts.tv_nsec= 100000000; // 100 ms
        for (int i = 0; i < 10 && running; ++i)
        {
            nanosleep(&ts, NULL);
        }
    }

    remove_dir_recursive(cache_dir);
    rmdir(cache_dir);

    system("kitty +kitten icat --clear >/dev/null 2>&1");

    return 0;
}
