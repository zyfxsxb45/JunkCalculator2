const fs = require('fs');
const path = require('path');

const sourcePath = path.join(__dirname, '../data/documentation.json');
const targetPath = path.join(__dirname, 'documentation.json');

try {
    const rawData = fs.readFileSync(sourcePath, 'utf-8');
    const doc = JSON.parse(rawData);
    
    // 删除不需要的 topics 节点以精简插件体积
    if (doc.topics) delete doc.topics;
    
    // 剔除描述与示例以精简体积，并将别名提升为独立的函数条目
    if (doc.functions) {
        const aliasesToAdd = {};
        for (const key in doc.functions) {
            const func = doc.functions[key];
            if (func.aliases) {
                for (const alias of func.aliases) {
                    // 将签名中的原函数名替换为别名 (例如 log(x) -> ln(x))
                    aliasesToAdd[alias] = {
                        signature: func.signature ? func.signature.split(key).join(alias) : alias
                    };
                }
                delete func.aliases;
            }
            delete func.desc;
            delete func.examples;
        }
        Object.assign(doc.functions, aliasesToAdd);
    }
    
    // 同样剔除关键字的描述与示例
    if (doc.keywords) {
        for (const key in doc.keywords) {
            delete doc.keywords[key].desc;
            delete doc.keywords[key].examples;
        }
    }
    
    fs.writeFileSync(targetPath, JSON.stringify(doc, null, 2), 'utf-8');
    console.log('Successfully built minified documentation.json for extension.');
} catch (err) {
    console.error('Error building documentation.json:', err);
    process.exit(1);
}
