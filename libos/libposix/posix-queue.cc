// -*- mode: c++; c-file-style: "k&r"; c-basic-offset: 4 -*-
/***********************************************************************
 *
 * libos/posix/posix-queue.cc
 *   POSIX implementation of Zeus queue interface
 *
 * Copyright 2018 Irene Zhang  <irene.zhang@microsoft.com>
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **********************************************************************/

#include "posix-queue.h"
#include "common/library.h"
#include "../include/measure.h"
// hoard include
#include "libzeus.h"
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <assert.h>
#include <string.h>
#include <errno.h>
#include <sys/uio.h>


namespace Zeus {

namespace POSIX {

int
PosixQueue::queue(int domain, int type, int protocol)
{
    int qd = ::socket(domain, type, protocol);

    if (qd != -1) {
        if (type == SOCK_STREAM) {
            // Set TCP_NODELAY
            int n = 1;
            if (setsockopt(qd, IPPROTO_TCP,
                           TCP_NODELAY, (char *)&n, sizeof(n)) < 0) {
                fprintf(stderr, 
                        "Failed to set TCP_NODELAY on Zeus connecting socket");
            }
        }
		else if (type == SOCK_DGRAM) {
			if (fcntl(qd, F_SETFL, O_NONBLOCK, 1)) {
				fprintf(stderr,
						"Failed to set O_NONBLOCK on outgoing Zeus socket");
			}
			int enable = 1;
			if (setsockopt(qd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
			    fprintf(stderr, "Failed to set SO_REUSEADDR on Zeus socket: %s\n",
			    		strerror(errno));
			}
			enable = 1;
			if (setsockopt(qd, SOL_SOCKET, SO_REUSEPORT, &enable, sizeof(int)) < 0) {
			    fprintf(stderr, "Failed to set SO_REUSEPORT on Zeus socket: %s\n",
			    		strerror(errno));
			}
		}

    }
    return qd;
}

int
PosixQueue::bind(struct sockaddr *saddr, socklen_t size)
{
    int res = ::bind(qd, saddr, size);
    if (res == 0) {
        return res;
    } else {
        return errno;
    }
}

int
PosixQueue::accept(struct sockaddr *saddr, socklen_t *size)
{
    int newqd = ::accept(qd, saddr, size);
    if (newqd != -1) {
        // Set TCP_NODELAY
        int n = 1;
        if (setsockopt(newqd, IPPROTO_TCP,
                       TCP_NODELAY, (char *)&n, sizeof(n)) < 0) {
            fprintf(stderr, 
                    "Failed to set TCP_NODELAY on Zeus connecting socket");
        }
        // Always put it in non-blocking mode
        if (fcntl(newqd, F_SETFL, O_NONBLOCK, 1)) {
            fprintf(stderr,
                    "Failed to set O_NONBLOCK on outgoing Zeus socket");
        }

    }
    return newqd;
}

int
PosixQueue::listen(int backlog)
{
    int res = ::listen(qd, backlog);
    if (res == 0) {
        return res;
    } else {
        return errno;
    }
}
        

int
PosixQueue::connect(struct sockaddr *saddr, socklen_t size)
{
    int res = ::connect(qd, saddr, size);
    //fprintf(stderr, "res = %i errno=%s", res, strerror(errno));
    if (res == 0) {
        // Always put it in non-blocking mode
        if (fcntl(qd, F_SETFL, O_NONBLOCK, 1)) {
            fprintf(stderr,
                    "Failed to set O_NONBLOCK on outgoing Zeus socket");
        }
        connected = true;
        return res;
    } else {
        return errno;
    }
}

int
PosixQueue::open(const char *pathname, int flags)
{
    // use the fd as qd
    int qd = ::open(pathname, flags);
    return qd;
}

int
PosixQueue::open(const char *pathname, int flags, mode_t mode)
{
    // use the fd as qd
    int qd = ::open(pathname, flags, mode);
    return qd;
}

int
PosixQueue::creat(const char *pathname, mode_t mode)
{
    // use the fd as qd
    int qd = ::creat(pathname, mode);
    return qd;
}
    
int
PosixQueue::close()
{
    return ::close(qd);
}

int
PosixQueue::fd()
{
    return qd;
}

void
PosixQueue::ProcessIncoming(PendingRequest &req)
{
    ssize_t count = 0;
    size_t dataLen;
    void* buf;

    double rx_start = zeus_rdtsc();
    //printf("ProcessIncoming qd:%d\n", qd);
    // if we don't have a full header in our buffer, then get one
    if (req.num_bytes < sizeof(req.header)) {
        if (type == UDP_Q) {
            req.buf = malloc(1024);
            assert(req.buf != NULL);
            
            socklen_t size = sizeof(struct sockaddr_in);
            struct sockaddr addr;
            count = ::recvfrom(qd, req.buf, 1024, 0, &addr, &size);
            req.sga->addr.sin_addr.s_addr = ((struct sockaddr_in*)&addr)->sin_addr.s_addr;
            req.sga->addr.sin_port = ((struct sockaddr_in*)&addr)->sin_port;
        } else {
            count = ::read(qd, (uint8_t *)&req.header + req.num_bytes,
                                   sizeof(req.header) - req.num_bytes);

        }
        //printf("ProcessIncoming: first read :%d\n", count);
        // we still don't have a header
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            } else {
                fprintf(stderr, "Could not read header: %s\n", strerror(errno));
                req.isDone = true;
                req.res = count;
                return;
            }
        }
        req.num_bytes += count;
        if (req.num_bytes < sizeof(req.header)) {
            return;
        }

        if (type == UDP_Q) {
            memcpy(&req.header, req.buf, sizeof(req.header));
        }
    }

    if (req.header[0] != MAGIC) {
        // not a correctly formed packet
        fprintf(stderr, "Could not find magic %lx\n", req.header[0]);
        req.isDone = true;
        req.res = -1;
        return;
    }

    dataLen = req.header[1];

    if (type == TCP_Q) {
        // now we'll allocate a buffer
        if (req.buf == NULL) {
            req.buf = malloc(dataLen);
        }

        size_t offset = req.num_bytes - sizeof(req.header);
        // grab the rest of the packet
        if (req.num_bytes < sizeof(req.header) + dataLen) {
            ssize_t count = ::read(qd, (uint8_t *)req.buf + offset,
                               dataLen - offset);
            if (count < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    return;
                } else {
                    fprintf(stderr, "Could not read data: %s\n", strerror(errno));
                    req.isDone = true;
                    req.res = count;
                    return;
                }
            }
            req.num_bytes += count;
            if (req.num_bytes < sizeof(req.header) + dataLen) {
                return;
            }
        }
    }

    buf = (type == TCP_Q) ? req.buf : req.buf + sizeof(req.header);

    // now we have the whole buffer, start filling sga
    uint8_t *ptr = (uint8_t *)buf;
    req.sga->num_bufs = req.header[2];
    for (int i = 0; i < req.sga->num_bufs; i++) {
        req.sga->bufs[i].len = *(size_t *)ptr;
        ptr += sizeof(uint64_t);
        req.sga->bufs[i].buf = (ioptr)ptr;
        ptr += req.sga->bufs[i].len;

#if DEBUG_POSIX_QUEUE
        printf("received: [%lu] bytes: %s\n", req.sga->bufs[i].len, (char*)req.sga->bufs[i].buf);
#endif
    }
    if (req.buf != NULL) {
    	free(req.buf);
    	req.buf = NULL;
    }

    double rx_end = zeus_rdtsc();

    printf("ProcessIncoming\n\tTotal Latency: %4.2f\n",
    		rx_end - rx_start);

    req.isDone = true;
    req.res = dataLen - (req.sga->num_bufs * sizeof(uint64_t));

    return;
}
    
void
PosixQueue::ProcessOutgoing(PendingRequest &req)
{
    sgarray sga = *(req.sga);
    //printf("req.num_bytes = %lu req.header[1] = %lu", req.num_bytes, req.header[1]);
    // set up header

    struct iovec vsga[2*sga.num_bufs + 1];
    uint64_t lens[sga.num_bufs];
    size_t dataSize = 0;
    size_t totalLen = 0;

    double tx_start = zeus_rdtsc();

    // calculate size and fill in iov
    for (int i = 0; i < sga.num_bufs; i++) {
        lens[i] = sga.bufs[i].len;
        vsga[2*i+1].iov_base = &lens[i];
        vsga[2*i+1].iov_len = sizeof(uint64_t);
        
        vsga[2*i+2].iov_base = (void *)sga.bufs[i].buf;
        vsga[2*i+2].iov_len = sga.bufs[i].len;
        // add up actual data size
        dataSize += (uint64_t)sga.bufs[i].len;
        
        // add up expected packet size minus header
        totalLen += (uint64_t)sga.bufs[i].len;
        totalLen += sizeof(uint64_t);
#if DEBUG_POSIX_QUEUE
        printf("sending:  [%lu] bytes: %s\n", (size_t)lens[i], (char*)vsga[2*i+2].iov_base);
#endif
        pin((void *)sga.bufs[i].buf);
    }

    // fill in header
    req.header[0] = MAGIC;
    req.header[1] = totalLen;
    req.header[2] = req.sga->num_bufs;

    // set up header at beginning of packet
    vsga[0].iov_base = &req.header;
    vsga[0].iov_len = sizeof(req.header);
    totalLen += sizeof(req.header);

    if (type == UDP_Q) {
    	struct sockaddr* addr = (struct sockaddr*)&req.sga->addr;
  		if (!connected && ::connect(qd, addr, sizeof(struct sockaddr_in)) < 0) {
			fprintf(stderr, "Could not connect to outgoing address: %s\n",
					strerror(errno));
			req.res = -1;
			req.isDone = true;
			return;
		}
  		struct sockaddr_in* peer = (struct sockaddr_in*)malloc(sizeof(struct sockaddr_in));
  		socklen_t size = sizeof(struct sockaddr_in);
  		if (::getpeername(qd, (struct sockaddr*)peer, &size) < 0) {
  			fprintf(stderr, "Could not get peer name: %s\n", strerror(errno));
  			req.res = -1;
  			req.isDone = true;
  		}
#if DEBUG_POSIX_QUEUE
  		printf("connected to: %x:%d\n", peer->sin_addr.s_addr, peer->sin_port);
#endif
  		free(peer);
    }
    ssize_t count = ::writev(qd,
                             vsga,
                             2*sga.num_bufs +1);
   
    // if error
    if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        } else {
            fprintf(stderr, "Could not write packet: %s\n", strerror(errno));
            req.isDone = true;
            req.res = count;
            return;
        }
    }

#if DEBUG_POSIX_QUEUE
    printf("%s\n", strerror(errno));
#endif

    // otherwise
    req.num_bytes += count;
    if (req.num_bytes < totalLen) {
        assert(req.num_bytes == 0);
        return;
    }
    for (int i = 0; i < sga.num_bufs; i++) {
        unpin((void *)sga.bufs[i].buf);
    }

    double tx_end = zeus_rdtsc();

    printf("ProcessOutgoing\n\tTotal Latency: %4.2f\n",
    		tx_end - tx_start);

    req.res = dataSize;
    req.isDone = true;
}
    
void
PosixQueue::ProcessQ(size_t maxRequests)
{
    size_t done = 0;
   
    while (!workQ.empty() && done < maxRequests) {
        qtoken qt = workQ.front();
        auto it = pending.find(qt);
        done++;
        if (it == pending.end()) {
            workQ.pop_front();
            continue;
        }
        
        PendingRequest &req = it->second; 
        if (IS_PUSH(qt)) {
            ProcessOutgoing(req);
        } else {
            ProcessIncoming(req);
        }

        if (req.isDone) {
            workQ.pop_front();
        }            
    }
    //printf("Processed %lu requests", done);
}
    
ssize_t
PosixQueue::Enqueue(qtoken qt, struct sgarray &sga)
{

    auto it = pending.find(qt);
    PendingRequest req;
    if (it == pending.end()) {
        req = PendingRequest();
        req.sga = &sga;
        req.sga->addr.sin_family = AF_INET;
        pending[qt] = req;
        workQ.push_back(qt);
        // let's try processing here because we know our sockets are
        // non-blocking
        if (workQ.front() == qt) ProcessQ(1);
    }

    req = pending.find(qt)->second;

    if (req.isDone) {
    	int ret = req.res;
    	assert(sga.num_bufs > 0);
        return ret;
    } else {
        return 0;
    }
}

ssize_t
PosixQueue::push(qtoken qt, struct sgarray &sga)
{
    return Enqueue(qt, sga);
}
    
ssize_t
PosixQueue::pop(qtoken qt, struct sgarray &sga)
{
    ssize_t newqt = Enqueue(qt, sga);
    return newqt;
}

ssize_t
PosixQueue::peek(qtoken qt, struct sgarray &sga)
{
	PendingRequest req = PendingRequest();
	req.sga = &sga;
	req.sga->addr.sin_family = AF_INET;
	ProcessIncoming(req);
	if (req.isDone){
		sga = *(req.sga);
		return req.res;
	}else{
		return -1;
	}
}
    
ssize_t
PosixQueue::wait(qtoken qt, struct sgarray &sga)
{
    ssize_t ret;
    auto it = pending.find(qt);
    assert(it != pending.end());

    while(!it->second.isDone) {
        ProcessQ(1);
    }
    sga = *it->second.sga;
    ret = it->second.res;
    return ret;
}

ssize_t
PosixQueue::poll(qtoken qt, struct sgarray &sga)
{
    auto it = pending.find(qt);
    assert(it != pending.end());
    if (it->second.isDone) {
    	int ret = it->second.res;
        sga = *(it->second.sga);
        return ret;
    } else {
        return 0;
    }
}


} // namespace POSIX    
} // namespace Zeus
