# Rhebok

[![Build Status](https://travis-ci.org/kazeburo/rhebok.svg?branch=master)](https://travis-ci.org/kazeburo/rhebok)

Rhebok is High Performance Rack Handler/Web Server. 2x performance when compared against Unicorn.

Rhebok supports following features.

- ultra fast HTTP processing using [picohttpparser](https://github.com/h2o/picohttpparser)
- uses accept4(2) if OS support
- uses writev(2) for output responses
- prefork and graceful shutdown using [prefork_engine](https://rubygems.org/gems/prefork_engine)
- hot deploy using [start_server](https://metacpan.org/release/Server-Starter) ([here](https://github.com/lestrrat/go-server-starter) is golang version by lestrrat-san)
- supports HTTP/1.1 except for KeepAlive
- supports OobGC

This server is suitable for running HTTP application servers behind a reverse proxy like nginx.

## Installation

Add this line to your application's Gemfile:

```
gem 'rhebok'
```

And then execute:

    $ bundle

Or install it yourself as:

    $ gem install rhebok

## Usage

    $ rackup -s Rhebok --port 8080 -O MaxWorkers=5 -O MaxRequestPerChild=1000 -O OobGC=yes -E production config.ru

## Sample configuration with Nginx

nginx.conf

    http {
      upstream app {
        server unix:/path/to/app.sock;
      }
      server {
        location / {
          proxy_pass http://app;
        }
        location ~ ^/(stylesheets|images)/ {
          root /path/to/webapp/public;
        }
      }
    }

command line of running Rhebok

    $ rackup -s Rhebok -O Path=/path/to/app.sock \
      -O MaxWorkers=5 -O MaxRequestPerChild=1000 -E production config.ru

## Options

### ConfigFile

filename to load options. For details, please read `Config File` section

### Host

hostname or ip address to bind (default: 0.0.0.0)

### Port

port to bind (default: 9292)

### Path

path to listen using unix socket

### BackLog

specifies a listen backlog parameter (default: Socket::SOMAXCONN. usually 128 on Linux )

### ReusePort

enable SO_REUSEPORT for TCP socket

### MaxWorkers

number of worker processes (default: 5)

### MaxRequestPerChild

Max number of requests to be handled before a worker process exits (default: 1000)
If set to `0`. worker never exists. This option looks like Apache's MaxRequestPerChild

### MinRequestPerChild

if set, randomizes the number of requests handled by a single worker process between the value and that supplied by MaxRequestPerChlid (default: none)

### Timeout

seconds until timeout (default: 300)

### OobGC

Boolean like string. If true, Rhebok execute GC after close client socket. (defualt: false)

### MaxGCPerRequest

If [gctools](https://github.com/tmm1/gctools) is available, this option is not used. invoke GC by `GC::OOB.run` after every requests.

Max number of request before invoking GC (defualt: 5)

### MinGCPerRequest

If set, randomizes the number of request before invoking GC between the number of MaxGCPerRequest (defualt: none)

### Chunked_Transfer

If set, use chunked transfer for response (default: false)

### SpawnInterval

if set, worker processes will not be spawned more than once than every given seconds. Also, when SIGUSR1 is being received, no more than one worker processes will be collected every given seconds. This feature is useful for doing a "slow-restart". See http://blog.kazuhooku.com/2011/04/web-serverstarter-parallelprefork.html for more information. (default: none)

## Config File

load options from specified config file. If same options are exists in command line and the config file, options written in the config file will take precedence.

```
$ cat rhebok_config.rb
host '127.0.0.1'
port 9292
max_workers ENV["WEB_CONCURRENCY"] || 3
before_fork {
  defined?(ActiveRecord::Base) and
    ActiveRecord::Base.connection.disconnect!
}
after_fork {
  defined?(ActiveRecord::Base) and
    ActiveRecord::Base.establish_connection
}
$ rackup -s Rhebok -O ConfigFile=rhebok_config.rb -O port 8080 -O OobGC -E production
Rhebok starts Listening on 127.0.0.1:9292 Pid:11892
```

Supported options in config file are below.

### host

### port

### path

### backlog

### reuseport

### max_workers

### timeout

### max_request_per_child

### min_request_per_child

### oobgc

### max_gc_per_request

### min_gc_per_request

### chunked_transfer

### spawn_interval

### before_fork

proc object. This block will be called by a master process before forking each worker

### after_fork

proc object. This block will be called by a worker process after forking

## Signal Handling

### Master process

- TERM, HUP: If the master process received TERM or HUP signal, Rhebok will shutdown gracefully
- USR1: If set SpawnInterval, Rhebok will collect workers every given seconds and exit

### worker process

- TERM: If the worker process received TERM, exit after finishing current request


## Benchmark

Rhebok and Unicorn "Hello World" Benchmark (behind nginx reverse proxy)

![image](https://s3-ap-northeast-1.amazonaws.com/softwarearchives/rhebok_unicorn_bench.png)

*"nginx static file" represents req/sec when delivering 13 bytes static files from nginx*

Application code is [here](https://github.com/kazeburo/rhebok_bench_app).

ruby version

    $ ruby -v
    ruby 2.1.5p273 (2014-11-13 revision 48405) [x86_64-linux]

nginx.conf

    worker_processes  16;

    events {
      worker_connections  50000;
    }

    http {
      include     mime.types;
      access_log  off;
      sendfile    on;
      tcp_nopush  on;
      tcp_nodelay on;
      etag        off;
      upstream app {
        server unix:/dev/shm/app.sock;
      }
      server {
        location / {
          proxy_pass http://app;
        }
      }
    }

### Rhebok

command to run

    $ start_server --backlog=16384 --path /dev/shm/app.sock -- \
      bundle exec --keep-file-descriptors rackup -s Rhebok \
        -O MaxWorkers=8 -O MaxRequestPerChild=0 -E production config.ru

#### Hello World/Rack Application

    $ ./wrk -t 4 -c 500 -d 30  http://localhost/
    Running 30s test @ http://localhost/
      4 threads and 500 connections
      Thread Stats   Avg      Stdev     Max   +/- Stdev
        Latency     1.74ms  661.27us  38.69ms   91.19%
        Req/Sec    72.69k     9.42k  114.33k    79.43%
      8206118 requests in 30.00s, 1.34GB read
    Requests/sec: 273544.70
    Transfer/sec:     45.90MB

#### Sinatra

    $ ./wrk -t 4 -c 500 -d 30  http://localhost/
    Running 30s test @ http://localhost/
      4 threads and 500 connections
      Thread Stats   Avg      Stdev     Max   +/- Stdev
        Latency    16.39ms  418.08us  22.25ms   78.25%
        Req/Sec     7.73k   230.81     8.38k    70.81%
      912104 requests in 30.00s, 273.09MB read
    Requests/sec:  30404.28
    Transfer/sec:      9.10MB

#### Rails

    $ ./wrk -t 4 -c 500 -d 30  http://localhost/
    Running 30s test @ http://localhost/
      4 threads and 500 connections
      Thread Stats   Avg      Stdev     Max   +/- Stdev
        Latency   101.13ms    2.57ms 139.04ms   96.88%
        Req/Sec     1.24k    27.11     1.29k    82.98%
      148169 requests in 30.00s, 178.18MB read
    Requests/sec:   4938.93
    Transfer/sec:      5.94MB

### Unicorn

unicorn.rb

    $ cat unicorn.rb
    worker_processes 8
    preload_app true
    listen "/dev/shm/app.sock"

command to run

    $ bundle exec unicorn -c unicorn.rb -E production config.ru

#### Hello World/Rack Application

    $ ./wrk -t 4 -c 500 -d 30  http://localhost/
    Running 30s test @ http://localhost/
      4 threads and 500 connections
      Thread Stats   Avg      Stdev     Max   +/- Stdev
        Latency     3.50ms  518.60us  30.28ms   90.35%
        Req/Sec    37.99k     4.10k   56.56k    72.14%
      4294095 requests in 30.00s, 786.07MB read
    Requests/sec: 143140.20
    Transfer/sec:     26.20MB

#### Sinatra

    $ ./wrk -t 4 -c 500 -d 30  http://localhost/
    Running 30s test @ http://localhost/
      4 threads and 500 connections
      Thread Stats   Avg      Stdev     Max   +/- Stdev
        Latency    19.31ms    1.09ms  65.92ms   89.94%
        Req/Sec     6.55k   312.92     7.24k    73.41%
      775712 requests in 30.00s, 244.09MB read
    Requests/sec:  25857.85
    Transfer/sec:      8.14MB

#### Rails

    $ ./wrk -t 4 -c 500 -d 30  http://localhost/
    Running 30s test @ http://localhost/
      4 threads and 500 connections
      Thread Stats   Avg      Stdev     Max   +/- Stdev
        Latency   105.70ms    4.70ms 151.11ms   93.50%
        Req/Sec     1.19k    44.16     1.25k    89.13%
      141846 requests in 30.00s, 172.74MB read
    Requests/sec:   4728.11
    Transfer/sec:      5.76MB

### Server Environment

I used EC2 for benchmarking. Instance type if c3.8xlarge(32cores). A benchmark tool and web servers were executed at same hosts. Before benchmark, increase somaxconn and nfiles.

## See Also

[Gazelle](https://metacpan.org/pod/Gazelle) Rhebok is created based on Gazelle code

[Server::Stater](https://metacpan.org/pod/Server::Starter)  a superdaemon for hot-deploying server programs

[picohttpparser](https://github.com/h2o/picohttpparser) fast http parser

## LICENSE

Copyright (C) Masahiro Nagano.

This library is free software; you can redistribute it and/or modify it under the same terms as Perl itself.

## Contributing

1. Fork it ( https://github.com/kazeburo/rhebok/fork )
2. Create your feature branch (`git checkout -b my-new-feature`)
3. Commit your changes (`git commit -am 'Add some feature'`)
4. Push to the branch (`git push origin my-new-feature`)
5. Create a new Pull Request
