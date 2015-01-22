require 'rack'
require File.expand_path('../testrequest', __FILE__)
require 'timeout'
require 'socket'
require 'rack/handler/rhebok'

describe Rhebok do
  extend TestRequest::Helpers
  begin

    @host = '127.0.0.1'
    @port = 9202
    #@app = Rack::Lint.new(TestRequest.new) Lint requires Content-Length or Transfer-Eoncoding
    @app = TestRequest.new
    @pid = fork
    if @pid == nil
      #child
      Rack::Handler::Rhebok.run(@app, :Host=>'127.0.0.1', :Port=>9202, :MaxWorkers=>1,
                                :BeforeFork => proc { ENV["TEST_FOO"] = "FOO" },
                                :AfterFork => proc { ENV["TEST_BAR"] = "BAR" },
                                )
      exit!(true)
    end

    command = 'curl  --stderr - -sv -X POST -T "'+File.expand_path('../testrequest.rb', __FILE__)+'" -H "Content-type: text/plain" --header "Transfer-Encoding: chunked" http://127.0.0.1:9202/remove_length'
    curl_command(command)
    should "with curl" do
      @request["Transfer-Encoding"].should.equal "chunked"
      @request["Expect"].should.equal "100-continue"
      @response.key?("Transfer-Encoding").should.equal false
      @response["CONTENT_LENGTH"].should.equal File.stat(File.expand_path('../testrequest.rb', __FILE__)).size.to_s
      @response["test.postdata"].bytesize.should.equal File.stat(File.expand_path('../testrequest.rb', __FILE__)).size
      @header["Transfer-Encoding"].should.equal "chunked"
      @header["Connection"].should.equal "close"
      @header.key?("HTTP/1.1 100 Continue").should.equal true
    end

  ensure
    sleep 1
    if @pid != nil
      Process.kill(:TERM, @pid)
      Process.wait()
    end
  end

end
