# Rhebok

This project is still in a very early development stage. IT IS NOT READY FOR PRODUCTION!

Rhebok is High Performance Rack Handler/Web Server. 2x performance when compared against Unicorn.

Rhebok supports following features.

- ultra fast HTTP processing using picohttpparser
- uses accept4(2) if OS support
- uses writev(2) for output responses
- prefork and graceful shutdown using prefork_engine
- hot deploy and unix domain socket using start_server
- only supports HTTP/1.0. But does not support Keepalive.

## Installation

Add this line to your application's Gemfile:

```ruby
gem 'rhebok'
```

And then execute:

    $ bundle

Or install it yourself as:

    $ gem install rhebok

## Usage

    $ rackup -s Rhebok -O Port=8080 -O MaxWorkers=10 -O MaxRequestPerChild=1000 -E production config.ru

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

    $ start_server --path /path/to/app.sock --backlog 16384 -- rackup -s Rhebok \
      -O MaxWorkers=10 -O MaxRequestPerChild=1000 -E production config.ru

## Options

### Host

hostname or ip address to bind

### Port

port to bind

### MaxWorkers

number of worker processes (default: 10)

### MaxRequestPerChild

max. number of requests to be handled before a worker process exits (default: 1000)
This option looks like Apache's MaxRequestPerChild

### MinRequestPerChild

if set, randomizes the number of requests handled by a single worker process between the value and that supplied by MaxRequestPerChlid (default: none)

### Timeout

seconds until timeout (default: 300)

### SpawnInterval

if set, worker processes will not be spawned more than once than every given seconds. Also, when SIGHUP is being received, no more than one worker processes will be collected every given seconds. This feature is useful for doing a "slow-restart". See http://blog.kazuhooku.com/2011/04/web-serverstarter-parallelprefork.html for more information. (default: none)

## Benchmark

Rhebok and Unicorn "Hello World" Benchmark (behind nginx reverse proxy)

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

config.ru

    class HelloApp
      def call(env)
        [ 
          200,
          { 'Content-Type' => 'text/html' },
          ['hello world ']
        ]
      end
    end
    run HelloApp.new

### Rhebok

command to run

    $ start_server --path /path/to/app.sock  -- rackup -s Rhebok \
      -O MaxWorkers=8 -O MaxRequestPerChild=1000000 -E production config.ru

result

    $ ./wrk -t 4 -c 500 -d 30  http://localhost/
    Running 30s test @ http://localhost/
      4 threads and 500 connections
      Thread Stats   Avg      Stdev     Max   +/- Stdev
        Latency     1.85ms    2.94ms 816.72ms   99.24%
        Req/Sec    70.14k     9.13k  110.33k    76.74%
      7885663 requests in 30.00s, 1.29GB read
    Requests/sec: 262864.06
    Transfer/sec:     44.11MB

### Unicorn

unicorn.rb

    $ cat unicorn.rb
    worker_processes 8
    preload_app true
    listen "/dev/shm/app.sock"

command to run

    $ unicorn -E production -c unicorn.rb config.ru
    
result

    $ ./wrk -t 4 -c 500 -d 30  http://localhost/
    Running 30s test @ http://localhost/
      4 threads and 500 connections
      Thread Stats   Avg      Stdev     Max   +/- Stdev
        Latency     3.57ms    0.95ms 206.42ms   94.66%
        Req/Sec    35.62k     3.79k   56.69k    77.38%
      4122935 requests in 30.00s, 754.74MB read
      Socket errors: connect 0, read 0, write 0, timeout 47
    Requests/sec: 137435.52
    Transfer/sec:     25.16MB

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
