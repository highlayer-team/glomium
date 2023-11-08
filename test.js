const Glomium = require("./")

console.log(
    new Glomium({
        gas: {
            limit: 1000000,
            memoryByteCost: 1
        }
    })
        .set("test", 5)
        .run(`(test+2)`)
)