#include <stdio.h>
#include <thread>

#include <sodtp_jitter.h>
#include <dtp_client.h>
#include <decode_video.h>
#include <stream_worker.h>
#include <sdl_play.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

#include "SDL2/SDL.h"
}

using namespace std;


void network_working(struct ev_loop *loop, ev_timer *w, int revents) {
    StreamWorker *worker = (StreamWorker*)w->data;
    worker->thd_conn = new thread(dtp_client, worker->host, worker->port, &worker->jbuffer);
}


void sdl_play(struct ev_loop *loop, ev_timer *w, int revents) {
    StreamWorker *worker = (StreamWorker*)w->data;
    // static int i = 0;
    // printf("play once, num = %d...\n", ++i);

    SDL_Rect &rect = worker->rect;
    SDLPlay &splay = worker->splay;

    SDL_PollEvent(&splay.event);
    // Quit SDL timer, and do not show videos.
    if (splay.event.type == SDL_QUIT) {
        SDL_Quit();
        ev_timer_stop(loop, w);
        return;
    }

    int i = 0;
    for (auto &ptr : worker->jbuffer.jptrs) {
        // Warn: decoder is not added in SodtpJitter now.
        if (i == 0) {
            rect.x = 0;
            rect.y = 0;
            rect.w = 720;
            rect.h = 400;
        }
        else {
            rect.x = 720;
            rect.y = 200 * (i-1);
            rect.w = 300;
            rect.h = 200;
        }

        // lock the pFrameShow
        scoped_lock lock(ptr->decoder.mutex);
        if (ptr->decoder.iStart) {
            splay.update(ptr->decoder.pFrameShow, ptr->decoder.pTexture, &rect);
        }
        i++;
    }
    splay.show();
}


int main(int argc, char *argv[]) {

    StreamWorker sworker;

    int screen_w = 720+300;
    int screen_h = 400;
    sworker.splay.init(screen_w, screen_h);

    const char *host = argv[1];
    const char *port = argv[2];

    const char *path = NULL;
    if (argc >= 4) {
        path = argv[3];
    }

    SaveConfig scon;
    scon.parse("./config/save.conf");
    if (!path) {
        path = scon.path.c_str();
    }
    if (!scon.save) {
        path = NULL;
    }

    sworker.host = host;
    sworker.port = port;
    sworker.path = path;

    struct ev_loop *loop = ev_default_loop(0);
    ev_signal watcher;
    ev_signal_init(&watcher, stream_working, SIGUSR1);
    ev_signal_start(loop, &watcher);
    watcher.data = &sworker;

    ev_timer player;
    ev_timer_init(&player, sdl_play, 0, 0.04);
    ev_timer_start(loop, &player);
    player.data = &sworker;

    ev_timer networker;
    ev_timer_init(&networker, network_working, 0, 0);
    ev_timer_start(loop, &networker);
    networker.data = &sworker;

    ev_run(loop, 0);

    SDL_Quit();

    printf("clear network thread.\n");
    sworker.thd_conn->join();

    // No lock here!
    // When network thread stops, jptrs will not be modified.
    // So we need not to lock the jptrs at the end of main().
    // Warning! If jptrs might be changed in other threads,
    // you have to lock the jptrs.
    // Usage example:
    // scoped_lock lock(jbuffer.mtx);
    for (auto it : sworker.jbuffer.jptrs) {
        it->set_state(SodtpJitter::STATE_CLOSE);
    }

    printf("total decoding thread number %lu\n", sworker.thds.size());
    for (auto it : sworker.thds) {
        it->join();
        printf("clear one decoding thread\n");
    }

    printf("main thread exit now\n");
    return 0;
}


