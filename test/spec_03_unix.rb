require 'rack'
require File.expand_path('../testrequest', __FILE__)
require 'timeout'
require 'socket'
require 'rack/handler/rhebok'

describe Rhebok do
  extend TestRequest::Helpers
  begin

    @path = "/tmp/app_spec_03_unix.sock"
    @app = Rack::Lint.new(TestRequest.new)
    @pid = fork
    if @pid == nil
      #child
      Rack::Handler::Rhebok.run(@app, :Path=>@path, :MaxWorkers=>1)
      exit!(true)
    end
    sleep 1

    c = UNIXSocket.open(@path)
    c.write("GET / HTTP/1.0\r\n\r\n");
    outbuf = ""
    c.read(nil,outbuf)
    header,body = outbuf.split(/\r\n\r\n/,2)
    response = YAML.load(body)

    should "unix domain" do
      response["rack.version"].should.equal [1,1]
      response["rack.url_scheme"].should.equal "http"
      response["SERVER_NAME"].should.equal "0.0.0.0"
      response["SERVER_PORT"].should.equal "0"
      response["REMOTE_ADDR"].should.equal ""
      response["REMOTE_PORT"].should.equal "0"
    end

  ensure
    sleep 1
    if @pid != nil
      Process.kill(:TERM, @pid)
      Process.wait()
    end
  end

end
