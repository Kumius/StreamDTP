# StreamDTP

Streaming over Deadline-aware Transport Protocol.


## Introduction
This is a project finished in 2020. After finishing [DTP paper](https://dl.acm.org/doi/pdf/10.1145/3343180.3343191), I built a simple realtime streaming system to further evalute the performance of DTP, and explore its potential in improving upper layer applications.
For the details of DTP, please refer to our paper.

Note that partial code in this repo is built upon [QUICHE](https://github.com/cloudflare/quiche).


## Configure

Add a folder named config, which contains a **save.conf** and a **stream.conf**.

The format of **save.conf** is as follows:
- 1st line: whether save the decoded frames or not. values=[0, 1]
- 2nd line: the path to save the decoded frames.

The format of **stream.conf** is as follows:
- each line: the path to the stream file.
  (Currently we only display the first 3 streams.)


## Dependencies
- [FFMPEG](https://github.com/FFmpeg/FFmpeg)
- [DTP](https://github.com/STAR-Tsinghua/DTP)

My initial implement was based on another private [DTP repo](https://github.com/VMatrix1900/DTP) from VMatrix1900.

If you want to stream videos based on other transport protocols (e.g., QUIC), you can refer to **dtp_client.h** and **dtp_server.h** to implement your own client and server.


## Video format

This is a proof-of-concept system, which currently **only support 25FPS h265 video**.
Of course, **StreamDTP** can transport other format files by simple adaptation.

Currently we choose to sniff the format of the network flow.
While, you can just transfer the parameters of the codec to the client.
These parameters could be found in `avcodec_parameters_to_context()`.
In this way, you do not need to sniff the I-frames, which oftentimes fails.
Once the client receives the parameters, it can initialize its codec context and decode the stream.