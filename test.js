const Glomium = require("./");

(async () => {
  function wait(ms) {
    return new Promise(res => {
      setTimeout(res, ms)
    })
  };
  const glomium = new Glomium({
    gas: {
      limit: 20000000,       
      memoryByteCost: 1        
    }
  });
  setInterval(() => console.log(1), 100)
  await glomium.set("console",console);

  await glomium.set("wait", wait);

  glomium.run(`
  console.log("Hello")
  wait(1000)
  console.log("World")
  `)

})()
