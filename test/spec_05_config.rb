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
      Rack::Handler::Rhebok.run(@app, :BeforeFork=>proc { ENV["TEST_FOO"]="BAZ" }, :ConfigFile=>File.expand_path('../testconfig.rb', __FILE__))
      exit!(true)
    end
    sleep 1

    # test
    should "hook env" do
      GET("/")
      response["TEST_FOO"].should.equal "FOO"
      response["TEST_BAR"].should.equal "BAR"
    end

  ensure
    sleep 1
    if @pid != nil
      Process.kill(:TERM, @pid)
      Process.wait()
    end
  end

end
