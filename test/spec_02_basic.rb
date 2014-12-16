require 'rack/handler/rhebok'

describe Rhebok do
  should 'has a version number' do
    Rhebok::VERSION.should.not.equal nil
  end
end
