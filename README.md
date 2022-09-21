# StreamDTP

Streaming over Deadline-aware Transport Protocol.


## Introduction
This is a project finished in 2020. After finishing [DTP paper](https://dl.acm.org/doi/pdf/10.1145/3343180.3343191), I built a simple real-time streaming system to further evaluate the performance of DTP, and explore its potential in improving upper-layer applications.
For the details of DTP, please refer to our paper.

Note that partial code in this repo is built upon [QUICHE](https://github.com/cloudflare/quiche).


### Logic flow

There are a client and a server.
- The server is responsible for sending streams. Note that for simplicity, these streams are from local files rather than live videos.
  - Please refer to `dtp_server.cxx` for details.
- The client is responsible for receiving data, decoding frames, and playing them (optional).
  - We use a producer-consumer pattern on the client side. The network thread (producer) will receive, assemble, and store frames in a jitter buffer, and the stream thread (consumer) will fetch, decode, and play the frames.
  - Please refer to `dplay.cxx` for details.


## Dependencies
- [FFMPEG](https://github.com/FFmpeg/FFmpeg)
- [DTP](https://github.com/STAR-Tsinghua/DTP)
- [libev](https://github.com/enki/libev)

The implement of StreamDTP is based on another [DTP (private repo)](https://github.com/VMatrix1900/DTP) from VMatrix1900. I've not tested it on [DTP](https://github.com/STAR-Tsinghua/DTP).
If you want to stream videos based on other transport protocols (e.g., QUIC), you can refer to **dtp_client.h** and **dtp_server.h** to implement your own client and server.


## Configure

Add a folder named config, which contains a **save.conf** and a **stream.conf**.

The format of **save.conf** is as follows:
- 1st line: whether save the decoded frames or not. values=[0, 1]
- 2nd line: save path of the decoded frames.

The format of **stream.conf** is as follows:
- each line: the path to the stream file.
  (Currently, we only display the first 3 streams.)

Update the `DTP_DIR` in the makefile, which should be the path to `quiche.h`


## Video format

This is a proof-of-concept system, which currently **only supports 25FPS h265 video**.
Of course, **StreamDTP** can decode other format files by simple adaptation.

Currently, we choose to sniff the format of the network flow.
While, you can just transfer the parameters of the codec to the client.
These parameters could be found in `avcodec_parameters_to_context()`.
In this way, you do not need to sniff the I-frames, which oftentimes fails.
Once the client receives the parameters, it can initialize its codec context and decode the stream.


## Decoding logic
Here we use the DTP client as an example.
- The network thread keeps receiving stream data from the server, and saves the data in `BlockDataBuffer` temporarily.
- Once a frame is completely received, the network thread will push it to the `JitterBuffer`.
- The view (or play) thread keeps fetching frames from the `JitterBuffer` at the given frequency.
  - Currently, we set the view (or play) interval to be 40ms. Details can be found in `decode_video.h`.
  - In fact, you can find the frame rate after decoding the stream.
  - Or you can send metadata (or manifest file) to inform clients about stream properties.
