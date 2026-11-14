# Assinatura do executável (SignPath Foundation)

Sem assinatura, o Windows mostra "O Windows protegeu o computador" (SmartScreen) para quem
baixa o launcher pelo navegador. A SignPath Foundation assina projetos open source de graça;
o `publish-launcher.yml` já está pronto para usar a assinatura dela assim que o projeto for
aprovado. Até lá, as versões continuam saindo sem assinatura, como antes.

## O que esperar

- O publicador do arquivo assinado aparece como **SignPath Foundation**, não como o seu nome.
- A assinatura vem de um certificado com reputação já construída, o que costuma acabar com o
  aviso do SmartScreen. Ninguém consegue garantir isso: a Microsoft decide pela reputação e,
  nas primeiras versões, o aviso ainda pode aparecer para algumas pessoas.
- As atualizações automáticas não passam pelo SmartScreen (o launcher baixa e troca o próprio
  arquivo). O aviso afeta quem baixa pela primeira vez no navegador.
- Cada assinatura precisa ser aprovada por você no site da SignPath. O workflow espera até
  uma hora por essa aprovação.

## Passo a passo

### 1. Antes de pedir

1. **Ative a verificação em duas etapas (MFA) no GitHub.** A SignPath exige MFA de todo
   mundo do projeto, no GitHub e na SignPath.
2. **Política de privacidade:** já está em [`docs/PRIVACY.md`](PRIVACY.md). Ela é exigida porque
   o launcher fala com servidores sozinho (tempo de jogo, ranking, atualizações).
3. **Seção "Code signing policy" no README e nas releases.** A SignPath pede este texto na página
   do projeto e nas páginas de download. Coloque quando for aprovado (antes disso ele não é verdade):

   ```markdown
   ## Code signing policy

   Free code signing provided by [SignPath.io](https://about.signpath.io/),
   certificate by [SignPath Foundation](https://signpath.org/).

   - Committers and reviewers: [ryanbworks](https://github.com/ryanbworks)
   - Approvers: [ryanbworks](https://github.com/ryanbworks)

   Privacy policy: [docs/PRIVACY.md](docs/PRIVACY.md)
   ```

### 2. Pedir

Em https://signpath.org/apply, preencha com o repositório
`https://github.com/ryanbworks/native-game-launcher`. As condições deles: licença OSI (o projeto
é MIT), nenhum componente proprietário, projeto mantido e já lançado, build verificável a partir
do código (o GitHub Actions faz isso) e "uma certa reputação verificável". Eles podem recusar sem
dar motivo.

### 3. Depois de aprovado, na SignPath

1. Crie o projeto **Eternal Launcher** (o nome do produto precisa ser o do projeto).
2. Ligue o **GitHub.com** como trusted build system do projeto, seguindo
   https://docs.signpath.io/trusted-build-systems/github.
3. Crie a **artifact configuration**. O workflow envia um zip com um único
   `EternalLauncher.exe`, cujo recurso de versão traz `ProductName = Eternal Launcher`:

   ```xml
   <?xml version="1.0" encoding="utf-8"?>
   <artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
     <zip-file>
       <pe-file path="EternalLauncher.exe" product-name="Eternal Launcher">
         <authenticode-sign />
       </pe-file>
     </zip-file>
   </artifact-configuration>
   ```

4. Crie a **signing policy** de release (por exemplo `release-signing`) com você como approver.
5. Crie um usuário de CI e um **API token** para ele, com permissão de enviar pedidos ao projeto.

### 4. No GitHub

Em **Settings → Environments → production**:

| Tipo | Nome | Valor |
| --- | --- | --- |
| Secret | `SIGNPATH_API_TOKEN` | o token do usuário de CI |
| Variable | `SIGNPATH_ORGANIZATION_ID` | o id da organização na SignPath |
| Variable | `SIGNPATH_PROJECT_SLUG` | o slug do projeto |
| Variable | `SIGNPATH_SIGNING_POLICY_SLUG` | `release-signing` |

Pronto: na próxima vez que o **Publish launcher** rodar, ele envia o executável para a SignPath,
espera a sua aprovação, confere a assinatura (`Get-AuthenticodeSignature`) e só então gera o
manifesto da atualização, que guarda o hash do arquivo já assinado. Sem as variáveis, esses
passos são pulados.
