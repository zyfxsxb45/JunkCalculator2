const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const os = require('os');

let outputChannel = null;

function getExePath() {
    const config = vscode.workspace.getConfiguration('jc2');
    let exePath = config.get('executablePath', '');
    if (exePath && fs.existsSync(exePath)) return exePath;

    const isWin = process.platform === 'win32';
    const name = isWin ? 'JunkCalculator2.exe' : 'JunkCalculator2';
    const pathDirs = (process.env.PATH || '').split(isWin ? ';' : ':');
    for (const dir of pathDirs) {
        const full = path.join(dir, name);
        if (fs.existsSync(full)) return full;
    }
    return null;
}

function runFile(filePath, cwd, extraFlags = "") {
    const exePath = getExePath();
    if (!exePath) {
        vscode.window.showErrorMessage(
            'JC2 executable not found. Use "JC2: Set Executable Path" to configure.',
            'Set Path'
        ).then(choice => {
            if (choice === 'Set Path') vscode.commands.executeCommand('jc2.setExecutable');
        });
        return;
    }

    let terminal = vscode.window.terminals.find(t => t.name === 'JC2');
    if (terminal) { terminal.dispose(); }

    terminal = vscode.window.createTerminal({ name: 'JC2', cwd: cwd });
    terminal.show();

    const isWin = process.platform === 'win32';
    const defaultShell = vscode.env.shell || '';
    const isPowerShell = defaultShell.toLowerCase().includes('powershell') || 
                         defaultShell.toLowerCase().includes('pwsh');

    if (isPowerShell) {
        terminal.sendText(`& "${exePath}" ${extraFlags} --run "${filePath}"`);
    } else if (isWin) {
        terminal.sendText(`chcp 65001 >nul`);
        terminal.sendText(`"${exePath}" ${extraFlags} --run "${filePath}"`);
    } else {
        terminal.sendText(`"${exePath}" ${extraFlags} --run "${filePath}"`);
    }
}

function activate(context) {
    context.subscriptions.push(
        vscode.commands.registerCommand('jc2.run', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor || !editor.document.fileName.endsWith('.jc2')) return;
            editor.document.save().then(() => {
                runFile(editor.document.fileName, path.dirname(editor.document.fileName));
            });
        })
    );

    // ★ 新增：带探针执行
    context.subscriptions.push(
        vscode.commands.registerCommand('jc2.runProfile', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor || !editor.document.fileName.endsWith('.jc2')) return;
            editor.document.save().then(() => {
                runFile(editor.document.fileName, path.dirname(editor.document.fileName), "--profile");
            });
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('jc2.runSelection', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) return;
            const selection = editor.document.getText(editor.selection);
            if (!selection.trim()) return;
            const tmpFile = path.join(os.tmpdir(), '_jc2_selection.jc2');
            fs.writeFileSync(tmpFile, selection, 'utf-8');
            runFile(tmpFile, path.dirname(editor.document.fileName));
        })
    );

    context.subscriptions.push(
        vscode.commands.registerCommand('jc2.setExecutable', async () => {
            const result = await vscode.window.showOpenDialog({
                canSelectFiles: true, canSelectFolders: false,
                filters: { 'Executable': process.platform === 'win32' ? ['exe'] : ['*'] },
                title: 'Select JunkCalculator2 executable'
            });
            if (result && result.length > 0) {
                const config = vscode.workspace.getConfiguration('jc2');
                await config.update('executablePath', result[0].fsPath, vscode.ConfigurationTarget.Global);
                vscode.window.showInformationMessage(`JC2 executable updated.`);
            }
        })
    );
}

function deactivate() { if (outputChannel) outputChannel.dispose(); }
module.exports = { activate, deactivate };