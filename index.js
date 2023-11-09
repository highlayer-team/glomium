const duktapeBindings = require('./build/Release/duktape_bindings.node');

class Glomium {
    constructor(config) {
        this.gasLimit = config?.gas?.limit || 100000;
        this.memCostPerByte = config?.gas?.memoryByteCost || 1;
        this.context = duktapeBindings.createContext({ gasLimit: this.gasLimit, memCostPerByte: this.memCostPerByte })
        return this;
    }
    set(name,value) {
        duktapeBindings.setGlobal(this.context,name, value);
        return this;
    }
    get(name) {
        return duktapeBindings.getGlobal(this.context,name);
    }
    run(code) {
        return duktapeBindings.evalString(this.context,code);
    }
    clear() {
        return duktapeBindings.flushGlobal(this.context)
    }
    setGas({gasLimit, memoryByteCost,gasUsed}) {
        return duktapeBindings.setGas(this.context,{gasLimit:gasLimit,memCostPerByte:memoryByteCost,gasUsed:gasUsed})
    }
}
module.exports=Glomium