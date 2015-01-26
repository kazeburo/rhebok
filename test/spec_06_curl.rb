require 'rack'
require File.expand_path('../testrequest', __FILE__)
require 'timeout'
require 'socket'
require 'rack/handler/rhebok'

class ZeroStreamBody
  def self.each
    yield "Content"
    yield ""
    yield "Again"
    yield nil
    yield 0
  end
end

class VacantStreamBody
  def self.each
  end
end


describe Rhebok do
  extend TestRequest::Helpers

  @host = '127.0.0.1'
  @port = 9202

  test_rhebok(TestRequest.new, proc {
    command = 'curl  --stderr - -sv -X POST -T "'+File.expand_path('../testrequest.rb', __FILE__)+'" -H "Content-type: text/plain" --header "Transfer-Encoding: chunked" http://127.0.0.1:9202/remove_length'
    curl_yaml(command)
    should "chunked with curl" do
      @request["Transfer-Encoding"].should.equal "chunked"
      @request["Expect"].should.equal "100-continue"
      @response.key?("Transfer-Encoding").should.equal false
      @response["CONTENT_LENGTH"].should.equal File.stat(File.expand_path('../testrequest.rb', __FILE__)).size.to_s
      @response["test.postdata"].bytesize.should.equal File.stat(File.expand_path('../testrequest.rb', __FILE__)).size
      @header["Transfer-Encoding"].should.equal "chunked"
      @header["Connection"].should.equal "close"
      @header.key?("HTTP/1.1 100 Continue").should.equal true
    end

    command = 'curl  --stderr - --http1.0 -sv -X POST -T "'+File.expand_path('../testrequest.rb', __FILE__)+'" -H "Content-type: text/plain" --header "Transfer-Encoding: chunked" http://127.0.0.1:9202/remove_length'
    curl_yaml(command)
    should "chunked with curl http1.0" do
      @response.key?("Transfer-Encoding").should.equal false
      @request.key?("Expect").should.equal false
      @response["CONTENT_LENGTH"].should.equal File.stat(File.expand_path('../testrequest.rb', __FILE__)).size.to_s
      @response["test.postdata"].bytesize.should.equal File.stat(File.expand_path('../testrequest.rb', __FILE__)).size
      @header.key?("Transfer-Encoding").should.equal false
      @header["Connection"].should.equal "close"
      @header.key?("HTTP/1.1 100 Continue").should.equal false
    end
  })

  test_rhebok( proc {
    [200,{"Content-Type"=>"text/plain"},["Content","","Again",nil,0]]
  }, proc {
    command = 'curl  --stderr - -sv http://127.0.0.1:9202/'
    curl_request(command)
    should "zero length with curl" do
      @body.should.equal "ContentAgain0"
    end
    command = 'curl  --stderr - -sv --http1.0 http://127.0.0.1:9202/'
    curl_request(command)
    should "zero length with curl http/1.0" do
      @body.should.equal "ContentAgain0"
    end
  })

  test_rhebok( proc {
    [200,{"Content-Type"=>"text/plain"},[]]
  }, proc {
    command = 'curl  --stderr - -sv http://127.0.0.1:9202/'
    curl_request(command)
    should "vacant res with curl" do
      @body.should.equal ""
    end
    command = 'curl  --stderr - -sv --http1.0 http://127.0.0.1:9202/'
    curl_request(command)
    should "vacant rew with curl http/1.0" do
      @body.should.equal ""
    end
  })

  test_rhebok( proc {
    [200,{"Content-Type"=>"text/plain"},ZeroStreamBody]
  }, proc {
    command = 'curl  --stderr - -sv http://127.0.0.1:9202/'
    curl_request(command)
    should "zerostream length with curl" do
      @body.should.equal "ContentAgain0"
    end
    command = 'curl  --stderr - -sv --http1.0 http://127.0.0.1:9202/'
    curl_request(command)
    should "zerostream length with curl http/1.0" do
      @body.should.equal "ContentAgain0"
    end
  })

  test_rhebok( proc {
    [200,{"Content-Type"=>"text/plain"},VacantStreamBody]
  }, proc {
    command = 'curl  --stderr - -sv http://127.0.0.1:9202/'
    curl_request(command)
    should "vacantstream length with curl" do
      @body.should.equal ""
    end
    command = 'curl  --stderr - -sv --http1.0 http://127.0.0.1:9202/'
    curl_request(command)
    should "vacantstream length with curl http/1.0" do
      @body.should.equal ""
    end
  })

end
