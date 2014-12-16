require 'rack'
require File.expand_path('../testrequest', __FILE__)
require 'timeout'
require 'rack/handler/rhebok'

describe Rhebok do
  extend TestRequest::Helpers
  begin

    @host = '127.0.0.1'
    @port = 9202
    @app = Rack::Lint.new(TestRequest.new)
    @pid = fork
    if @pid == nil
      #child
      Rack::Handler::Rhebok.run(@app, :Host=>'127.0.0.1', :Port=>9202, :MaxWorkers=>1)
      exit!(true)
    end
    sleep 1
    # test
    should "respond" do
      GET("/")
      response.should.not.be.nil
    end


  ensure
   sleep 1
   if @pid != nil
     Process.kill(:TERM, @pid)
     Process.wait()
   end
  end

end


