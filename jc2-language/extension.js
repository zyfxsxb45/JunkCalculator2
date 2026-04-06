const vscode = require('vscode');
const path = require('path');
const fs = require('fs');
const os = require('os');
const { spawn } = require('child_process');

let outputChannel = null;

function getOutputChannel() {
    if (!outputChannel) {
        outputChannel = vscode.window.createOutputChannel('JC2');
    }
    return outputChannel;
}

function getExePath() {
    const config = vscode.workspace.getConfiguration('jc2');
    let exePath = config.get('executablePath', '');
    if (exePath && fs.existsSync(exePath)) return exePath;

    // 尝试从 PATH 中找
    const isWin = process.platform === 'win32';
    const name = isWin ? 'JunkCalculator2.exe' : 'JunkCalculator2';
    const pathDirs = (process.env.PATH || '').split(isWin ? ';' : ':');
    for (const dir of pathDirs) {
        const full = path.join(dir, name);
        if (fs.existsSync(full)) return full;
    }

    return null;
}

function runFile(filePath, cwd) {
    const exePath = getExePath();
    if (!exePath) {
        vscode.window.showErrorMessage(
            'JC2 executable not found. Use "JC2: Set Executable Path" to configure.',
            'Set Path'
        ).then(choice => {
            if (choice === 'Set Path') {
                vscode.commands.executeCommand('jc2.setExecutable');
            }
        });
        return;
    }

    // ★ 关掉旧终端防残留
    let terminal = vscode.window.terminals.find(t => t.name === 'JC2');
    if (terminal) {
        terminal.dispose();
    }

    terminal = vscode.window.createTerminal({
        name: 'JC2',
        cwd: cwd
    });
    terminal.show();

    // ★ 检测默认 shell 类型
    const isWin = process.platform === 'win32';
    const defaultShell = vscode.env.shell || '';
    const isPowerShell = defaultShell.toLowerCase().includes('powershell') || 
                         defaultShell.toLowerCase().includes('pwsh');

    if (isPowerShell) {
        // PowerShell 需要 & 调用运算符
        terminal.sendText(`& "${exePath}" --run "${filePath}"`);
    } else if (isWin) {
        // CMD
        terminal.sendText(`chcp 65001 >nul`);
        terminal.sendText(`"${exePath}" --run "${filePath}"`);
    } else {
        // Linux / macOS
        terminal.sendText(`"${exePath}" --run "${filePath}"`);
    }
}

function activate(context) {
    // ═══ Run Current Script ═══
    context.subscriptions.push(
        vscode.commands.registerCommand('jc2.run', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) {
                vscode.window.showWarningMessage('No active editor.');
                return;
            }
            if (!editor.document.fileName.endsWith('.jc2')) {
                vscode.window.showWarningMessage('Current file is not a .jc2 file.');
                return;
            }
            editor.document.save().then(() => {
                const filePath = editor.document.fileName;
                const cwd = path.dirname(filePath);
                runFile(filePath, cwd);
            });
        })
    );

    // ═══ Run Selection ═══
    context.subscriptions.push(
        vscode.commands.registerCommand('jc2.runSelection', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor) return;

            const selection = editor.document.getText(editor.selection);
            if (!selection.trim()) {
                vscode.window.showWarningMessage('No text selected.');
                return;
            }

            const tmpFile = path.join(os.tmpdir(), '_jc2_selection.jc2');
            fs.writeFileSync(tmpFile, selection, 'utf-8');

            const cwd = path.dirname(editor.document.fileName);
            runFile(tmpFile, cwd);
        })
    );

    // ═══ Set Executable Path ═══
    context.subscriptions.push(
        vscode.commands.registerCommand('jc2.setExecutable', async () => {
            const result = await vscode.window.showOpenDialog({
                canSelectFiles: true,
                canSelectFolders: false,
                canSelectMany: false,
                filters: {
                    'Executable': process.platform === 'win32' ? ['exe'] : ['*'],
                    'All': ['*']
                },
                title: 'Select JunkCalculator2 executable'
            });

            if (result && result.length > 0) {
                const exePath = result[0].fsPath;
                const config = vscode.workspace.getConfiguration('jc2');
                await config.update('executablePath', exePath, vscode.ConfigurationTarget.Global);
                vscode.window.showInformationMessage(`JC2 executable: ${exePath}`);
            }
        })
    );
}

function deactivate() {
    if (outputChannel) outputChannel.dispose();
}

module.exports = { activate, deactivate };
