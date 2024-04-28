const duktapeBindings = require('./build/Release/duktape_bindings.node');

class Glomium {
    constructor(config) {
        this.callbackMap=new Map()
        this.gasLimit = config?.gas?.limit || 100000;
        this.memCostPerByte = config?.gas?.memoryByteCost || 1;
        this.context = duktapeBindings.createContext({ gasLimit: this.gasLimit, memCostPerByte: this.memCostPerByte },this.__eventHandler.bind(this))
        this.functionRegistry=[]
        return this;
    }
    async set(name, value) {
        await this.__passToEngine({event:"setGlobal",globalValue:value,globalName:name})
        // duktapeBindings.setGlobal(this.context,name, this.__nodeValueToJson(value));
        return this;
    }

    async get(name) {
        return this.__parseValueFromEngine(JSON.stringify(await this.__passToEngine({ event: "getGlobal", globalName: name })),this)
    }
    async run(code) {

        return this.__parseValueFromEngine(JSON.stringify(await this.__passToEngine({ event: "eval", code:code })),this);
    }
    
    
    async clear() {
        const newContext = await this.__passToEngine({ event: "flushContext", newGas: { memCostPerByte: this.memCostPerByte, gasLimit: this.gasLimit } })

        this.context = duktapeBindings.__swapContexts(this.context, newContext)
        this.functionRegistry=[]
        return this;
    }
    async setGas({limit, memoryByteCost,used}) {
        return await this.__passToEngine({
            event: "setGas", gasData: {
                limit,
                memoryByteCost:memoryByteCost||0,
                used:used||0
        } })
    }
    async getGas() {
        return await this.__passToEngine({
            event: "getGas"
        })
    }

    __eventHandler(data) {

        const msg = JSON.parse(data)
        let event = ({
            "functionCall":async  () => {
                let execDataPointer=msg.executionDataPtr
                let res = await this.functionRegistry[msg.id](...msg.args)
                duktapeBindings.__notifyWaitingExecData(execDataPointer,this.__nodeValueToJson(res))
            },
            "callFinished": () => {
                // console.log("Node got:",msg)
                const prom = this.callbackMap.get(msg.callId)
                if(!msg.result&&msg.error){
                    prom.reject(msg.error)
                }else{
                    prom.resolve(msg.result);
                }
               
                this.callbackMap.delete(msg.callId)
            },
            "fatalError": () => {
        
                if (msg.gasInfo.gasUsed >= msg.gasInfo.gasLimit) {
                    this.callbackMap.get(msg.callId).reject({message:"Out of gas", gasInfo:msg.gasInfo})
                    this.callbackMap.delete(msg.callId)
                } else {
                    this.callbackMap.get(msg.callId).reject({ message: "Fatal error in execution engine", gasInfo: msg.gasInfo })
                    this.callbackMap.delete(msg.callId)
                    console.log("Fatal error in execution engine")

                }
            }
        })[msg.event];
        (event||(()=>{console.log("Call to unknown event ("+msg.event+")")}))()
    }
    __parseValueFromEngine(jsonval, engineClass) {
       
        function parseSpecialObjects(o) {
            let or;
            if (typeof o != "object" && !Array.isArray(o)) {
                or=o
            } else {
                if (Array.isArray(o)) {
                    or=o.map(parseSpecialObjects)
                } else {
                    if (o == null) {
                        or = null
                    }else 
                    if (typeof o.__engineInternalProperties == "object") {
                        or = (({
                            "function": () => {
                                return async (...args) => {

                                    return await engineClass.__passToEngine({ event: "callFunctionByPointer", pointer: o.__engineInternalProperties.heapptr, args:args})
                               
                            }
                        }})[o.__engineInternalProperties.type])()
                    } else {
                        or=Object.fromEntries(
                            Object.entries(o).map(([k, v]) => [k, parseSpecialObjects(v)])
                        )
                    }
                   
                }
            }
            return or
        }
        let jsonparsed = parseSpecialObjects(JSON.parse(jsonval))
        
        return jsonparsed
    }

    __nodeValueToJson(val) {
    const convertToJson = (value) => {
        if (value === null) {
            return null;
        } else if (value === undefined) {
            return {
                __engineInternalProperties: { type: 'undefined' }
            };
        } else if (typeof value === 'function') {
            return {
                __engineInternalProperties: {
                    type: 'function',
                    id: this.functionRegistry.push(value) - 1,
                    name: value.name
                }
            };
        } else if (typeof value === 'object') {
            if (Array.isArray(value)) {
                return value.map(item => convertToJson(item));
            } else {
                const obj = {};
                for (const [key, item] of Object.entries(value)) {
                    if (key !== '__engineInternalProperties') {
                        obj[key] = convertToJson(item);
                    }
                }
                return obj;
            }
        } else {
            return value;
        }
    };

    const jsonValue = convertToJson(val);
    return JSON.stringify(jsonValue);
    }
    __passToEngine(value) {
        return new Promise((re, rj) => {
            const id = (Date.now() + Math.floor(Math.random() * (10 ** 12))).toString(32)
            this.callbackMap.set(id, {resolve:re,reject:rj})
            duktapeBindings.__callThread(this.context, this.__nodeValueToJson({ ...value, callId: id }))
        })

       

    }
}
module.exports=Glomium