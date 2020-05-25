const restify = require('restify');
var needle = require('needle');
const config = require(__dirname + '/config.js');


const SERVER_HOST = config.SERVER_HOST;
const LOCAL_PORT = config.PORT;





const server = restify.createServer({
      name: 'loramock_proxy',
      version: '0.0.1'
});

server.use(restify.plugins.acceptParser(server.acceptable));
server.use(restify.plugins.queryParser());
server.use(restify.plugins.bodyParser());

server.get('/uplink', function(req, res, next) {
      needle('post', SERVER_HOST + '/uplink', {
                  payload: '',
                  port: '1'
            }, { json: true })
            .then(response => {
                  res.setHeader('content-type', 'application/json');
                  res.setHeader('Access-Control-Allow-Origin', '*');
                  res.setHeader('Access-Control-Allow-Methods', 'GET, POST');
                  console.log(response.body);
                  res.send(response.body);
                  return next();
            })
            .catch(error => {
                  console.log('An error occured:');
                  console.log(error);
            });
});



server.get('/downlink', function(req, res, next) {
      needle('get', SERVER_HOST + '/downlink', { json: true })
            .then(response => {
                  res.setHeader('content-type', 'application/json');
                  res.setHeader('Access-Control-Allow-Origin', '*');
                  res.setHeader('Access-Control-Allow-Methods', 'GET, POST');
                  console.log(response.body);
                  res.send(response.body);
                  return next();
            })
            .catch(error => {
                  console.log(error);
            });
});


server.listen(LOCAL_PORT, function() {
      console.log('%s listening at %s', server.name, server.url);
});
