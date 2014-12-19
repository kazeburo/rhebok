# -*- coding: utf-8 -*-

$LOAD_PATH.unshift File.expand_path('../../lib', __FILE__)
require 'rubygems'
require 'rack'
require 'stringio'
require 'tempfile'
require 'socket'
require 'rack/utils'
require 'io/nonblock'
require 'prefork_engine'
require 'rhebok'

module Rack
  module Handler
    class Rhebok
      MAX_MEMORY_BUFFER_SIZE = 1024 * 1024
      DEFAULT_OPTIONS = {
        :Host => '0.0.0.0',
        :Port => 9292,
        :MaxWorkers => 10,
        :Timeout => 300,
        :MaxRequestPerChild => 1000,
        :MinRequestPerChild => nil,
        :SpawnInterval => nil,
        :ErrRespawnInterval => nil
      }
      NULLIO  = StringIO.new("").set_encoding('BINARY')

      def self.run(app, options={})
        slf = new(options)
        slf.setup_listener()
        slf.run_worker(app)
      end

      def self.valid_options
        {
          "Host=HOST" => "Hostname to listen on (default: 0.0.0.0)",
          "Port=PORT" => "Port to listen on (default: 9292)",
        }
      end

      def initialize(options={})
        @options = DEFAULT_OPTIONS.merge(options)
        @server = nil
        @_is_tcp = false
        @_using_defer_accept = false
      end

      def setup_listener()
        if ENV["SERVER_STARTER_PORT"]
          hostport, fd = ENV["SERVER_STARTER_PORT"].split("=",2)
          if m = hostport.match(/(.*):(\d+)/)
            @options[:Host] = m[0]
            @options[:Port] = m[1].to_i
          else
            @options[:Port] = hostport
          end
          @server = TCPServer.for_fd(fd.to_i)
          @_is_tcp = true if !@server.local_address.unix?
        end

        if @server == nil
          puts "Rhebok starts Listening on #{@options[:Host]}:#{@options[:Port]} Pid:#{$$}"
          @server = TCPServer.new(@options[:Host], @options[:Port])
          @server.setsockopt(:SOCKET, :REUSEADDR, 1)
          @_is_tcp = true
        end

        if RUBY_PLATFORM.match(/linux/) && @_is_tcp == true
          begin
            @server.setsockopt(Socket::IPPROTO_TCP, 9, 1)
            @_using_defer_accept = true
          end
        end
      end

      def run_worker(app)
        pm_args = {
          "max_workers" => @options[:MaxWorkers].to_i,
          "trap_signals" => {
            "TERM" => 'TERM',
            "HUP"  => 'TERM',
          },
        }
        if @options[:SpawnInterval]
          pm_args["trap_signals"]["USR1"] = ["TERM", @options[:SpawnInterval].to_i]
          pm_args["spawn_interval"] = @options[:SpawnInterval].to_i
        end
        if @options[:ErrRespawnInterval]
          pm_args["err_respawn_interval"] = @options[:ErrRespawnInterval].to_i
        end
        Signal.trap('INT','SYSTEM_DEFAULT') # XXX

        pe = PreforkEngine.new(pm_args)
        while !pe.signal_received.match(/^(TERM|USR1)$/)
          pe.start do
            srand
            self.accept_loop(app)
          end
        end
        pe.wait_all_children
      end

      def _calc_reqs_per_child
        max = @options[:MaxRequestPerChild].to_i
        min = @options[:MinRequestPerChild].to_i
        if min < max
          max - ((max - min + 1) * rand).to_i
        else
          max
        end
      end

      def accept_loop(app)
        @can_exit = true
        @term_received = 0
        proc_req_count = 0
        Signal.trap(:TERM) do
          @term_received += 1
          if @can_exit
            exit!(true)
          end
          if @can_exit || @term_received > 1
            exit!(true)
          end
        end
        Signal.trap(:PIPE, "IGNORE")
        max_reqs = self._calc_reqs_per_child()
        fileno = @server.fileno

        env_template = {
          "SERVER_NAME"       => @options[:Host],
          "SERVER_PORT"       => @options[:Port].to_s,
          "rack.version"      => [1,1],
          "rack.errors"       => STDERR,
          "rack.multithread"  => false,
          "rack.multiprocess" => true,
          "rack.run_once"     => false,
          "rack.url_scheme"   => "http",
          "rack.input"        => NULLIO
        }

        while @options[:MaxRequestPerChild].to_i == 0 || proc_req_count < max_reqs
          @can_exit = true
          env = env_template.clone
          connection, buf = ::Rhebok.accept_rack(fileno, @options[:Timeout], @_is_tcp, env)
          if connection
            # for tempfile
            buffer = nil
            begin
              proc_req_count += 1
              @can_exit = false
              # handle request
              if env.key?("CONTENT_LENGTH") && env["CONTENT_LENGTH"].to_i > 0
                cl = env["CONTENT_LENGTH"].to_i
                if cl > MAX_MEMORY_BUFFER_SIZE
                  buffer = Tempfile.open('r')
                  buffer.binmode
                  buffer.set_encoding('BINARY')
                else
                  buffer = StringIO.new("").set_encoding('BINARY')
                end
                while cl > 0
                  chunk = ""
                  if buf.bytesize > 0
                    chunk = buf
                    buf = ""
                  else
                    readed = ::Rhebok.read_timeout(connection, chunk, cl, 0, @options[:Timeout])
                    if readed == nil
                      return
                    end
                  end
                  buffer << chunk
                  cl -= chunk.bytesize
                end
                buffer.rewind
                env["rack.input"] = buffer
              end

              status_code, headers, body = app.call(env)
              if body.instance_of?(Array)
                ::Rhebok.write_response(connection, @options[:Timeout], status_code, headers, body)
              else
                ::Rhebok.write_response(connection, @options[:Timeout], status_code, headers, [])
                body.each do |part|
                  ret = ::Rhebok.write_all(connection, part, 0, @options[:Timeout])
                  if ret == nil
                    break
                  end
                end #body.each
              end
              #p [env,status_code,headers,body]
            ensure
              if buffer.instance_of?(Tempfile)
                buffer.close!
              end
              ::Rhebok.close_rack(connection)
            end #begin
          end # accept
          if @term_received > 0
            exit!(true)
          end
        end #while max_reqs
      end #def

    end
  end
end
