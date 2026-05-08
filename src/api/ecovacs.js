import { lsGet, lsSet } from '../storage/agents-db.js';

// ─── MD5 compact (RFC 1321) ───────────────────────────────────────────────────
// Requis par l'API Ecovacs pour hasher le mot de passe avant envoi
function md5(str) {
  function safe(x, y) { return (x + y) | 0; }
  function rot(n, c)  { return (n << c) | (n >>> (32 - c)); }
  function cmn(q, a, b, x, s, t) { return safe(rot(safe(safe(a, q), safe(x, t)), s), b); }
  function ff(a,b,c,d,x,s,t) { return cmn((b&c)|(~b&d),a,b,x,s,t); }
  function gg(a,b,c,d,x,s,t) { return cmn((b&d)|(c&~d),a,b,x,s,t); }
  function hh(a,b,c,d,x,s,t) { return cmn(b^c^d,a,b,x,s,t); }
  function ii(a,b,c,d,x,s,t) { return cmn(c^(b|~d),a,b,x,s,t); }

  const s = unescape(encodeURIComponent(str));
  const nb = ((s.length + 8) >> 6) + 1;
  const blk = new Array(nb * 16).fill(0);
  for (let i = 0; i < s.length; i++) blk[i>>2] |= s.charCodeAt(i) << ((i%4)*8);
  blk[s.length>>2] |= 0x80 << ((s.length%4)*8);
  blk[nb*16-2] = s.length * 8;

  let [a, b, c, d] = [1732584193, -271733879, -1732584194, 271733878];
  for (let i = 0; i < blk.length; i += 16) {
    const [A,B,C,D] = [a,b,c,d];
    a=ff(a,b,c,d,blk[i],7,-680876936);    d=ff(d,a,b,c,blk[i+1],12,-389564586);
    c=ff(c,d,a,b,blk[i+2],17,606105819);  b=ff(b,c,d,a,blk[i+3],22,-1044525330);
    a=ff(a,b,c,d,blk[i+4],7,-176418897);  d=ff(d,a,b,c,blk[i+5],12,1200080426);
    c=ff(c,d,a,b,blk[i+6],17,-1473231341);b=ff(b,c,d,a,blk[i+7],22,-45705983);
    a=ff(a,b,c,d,blk[i+8],7,1770035416);  d=ff(d,a,b,c,blk[i+9],12,-1958414417);
    c=ff(c,d,a,b,blk[i+10],17,-42063);    b=ff(b,c,d,a,blk[i+11],22,-1990404162);
    a=ff(a,b,c,d,blk[i+12],7,1804603682); d=ff(d,a,b,c,blk[i+13],12,-40341101);
    c=ff(c,d,a,b,blk[i+14],17,-1502002290);b=ff(b,c,d,a,blk[i+15],22,1236535329);
    a=gg(a,b,c,d,blk[i+1],5,-165796510);  d=gg(d,a,b,c,blk[i+6],9,-1069501632);
    c=gg(c,d,a,b,blk[i+11],14,643717713); b=gg(b,c,d,a,blk[i+0],20,-373897302);
    a=gg(a,b,c,d,blk[i+5],5,-701558691);  d=gg(d,a,b,c,blk[i+10],9,38016083);
    c=gg(c,d,a,b,blk[i+15],14,-660478335);b=gg(b,c,d,a,blk[i+4],20,-405537848);
    a=gg(a,b,c,d,blk[i+9],5,568446438);   d=gg(d,a,b,c,blk[i+14],9,-1019803690);
    c=gg(c,d,a,b,blk[i+3],14,-187363961); b=gg(b,c,d,a,blk[i+8],20,1163531501);
    a=gg(a,b,c,d,blk[i+13],5,-1444681467);d=gg(d,a,b,c,blk[i+2],9,-51403784);
    c=gg(c,d,a,b,blk[i+7],14,1735328473); b=gg(b,c,d,a,blk[i+12],20,-1926607734);
    a=hh(a,b,c,d,blk[i+5],4,-378558);     d=hh(d,a,b,c,blk[i+8],11,-2022574463);
    c=hh(c,d,a,b,blk[i+11],16,1839030562);b=hh(b,c,d,a,blk[i+14],23,-35309556);
    a=hh(a,b,c,d,blk[i+1],4,-1530992060); d=hh(d,a,b,c,blk[i+4],11,1272893353);
    c=hh(c,d,a,b,blk[i+7],16,-155497632); b=hh(b,c,d,a,blk[i+10],23,-1094730640);
    a=hh(a,b,c,d,blk[i+13],4,681279174);  d=hh(d,a,b,c,blk[i+0],11,-358537222);
    c=hh(c,d,a,b,blk[i+3],16,-722521979); b=hh(b,c,d,a,blk[i+6],23,76029189);
    a=hh(a,b,c,d,blk[i+9],4,-640364487);  d=hh(d,a,b,c,blk[i+12],11,-421815835);
    c=hh(c,d,a,b,blk[i+15],16,530742520); b=hh(b,c,d,a,blk[i+2],23,-995338651);
    a=ii(a,b,c,d,blk[i+0],6,-198630844);  d=ii(d,a,b,c,blk[i+7],10,1126891415);
    c=ii(c,d,a,b,blk[i+14],15,-1416354905);b=ii(b,c,d,a,blk[i+5],21,-57434055);
    a=ii(a,b,c,d,blk[i+12],6,1700485571); d=ii(d,a,b,c,blk[i+3],10,-1894986606);
    c=ii(c,d,a,b,blk[i+10],15,-1051523);  b=ii(b,c,d,a,blk[i+1],21,-2054922799);
    a=ii(a,b,c,d,blk[i+8],6,1873313359);  d=ii(d,a,b,c,blk[i+15],10,-30611744);
    c=ii(c,d,a,b,blk[i+6],15,-1560198380);b=ii(b,c,d,a,blk[i+13],21,1309151649);
    a=ii(a,b,c,d,blk[i+4],6,-145523070);  d=ii(d,a,b,c,blk[i+11],10,-1120210379);
    c=ii(c,d,a,b,blk[i+2],15,718787259);  b=ii(b,c,d,a,blk[i+9],21,-343485551);
    [a,b,c,d] = [safe(a,A), safe(b,B), safe(c,C), safe(d,D)];
  }
  return [a,b,c,d].map(n => (n>>>0).toString(16).padStart(8,'0').match(/../g).reverse().join('')).join('');
}

// ─── Config API Ecovacs EU ────────────────────────────────────────────────────
const BASE_LOGIN = 'https://gl-eu-openapi.ecovacs.com/v1/private';
const BASE_IOT   = 'https://portal-eu.ecouser.net/api/iot/devmanager.do';
const APP_CODE   = 'i_eco_e';
const APP_VER    = '1.3.5';
const CHANNEL    = 'c_googleplay';

function getProxyUrl(target) {
  const base = (lsGet('SEARCH_PROXY_URL') || 'https://proxy.sicho95.workers.dev/').replace(/\/$/, '');
  return base + '/' + target;
}

// Connexion au compte Ecovacs — stocke did, token, uid en localStorage
export async function ecovacs_login(account, password) {
  const country    = 'DE';
  const lang       = 'en';
  const deviceId   = lsGet('ECOVACS_DEVICE_ID') || crypto.randomUUID().replace(/-/g, '');
  lsSet('ECOVACS_DEVICE_ID', deviceId);

  const loginPath = `${BASE_LOGIN}/${country}/${lang}/${deviceId}/${APP_CODE}/${APP_VER}/${CHANNEL}/1/user/login`;

  const body = new URLSearchParams({
    account,
    password:  md5(password).toLowerCase(),
    requestId: crypto.randomUUID().replace(/-/g, ''),
  });

  const res = await fetch(getProxyUrl(loginPath), {
    method:  'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body,
    signal:  AbortSignal.timeout(15000),
  });
  if (!res.ok) throw new Error('Login Ecovacs échoué : HTTP ' + res.status);
  const data = await res.json();
  if (data.ret !== 'ok') throw new Error('Login Ecovacs refusé : ' + (data.errno || data.ret));

  lsSet('ECOVACS_ACCOUNT', account);
  lsSet('ECOVACS_UID',      data.uid      || '');
  lsSet('ECOVACS_TOKEN',    data.accessToken || data.token || '');
  lsSet('ECOVACS_DID',      data.did      || lsGet('ECOVACS_DID') || '');
  lsSet('ECOVACS_RESOURCE', data.resource || '');
  return data;
}

// Envoyer une commande au robot via l'API IOT Ecovacs
export async function ecovacs_send_cmd(cmd, params = {}) {
  const uid      = lsGet('ECOVACS_UID');
  const token    = lsGet('ECOVACS_TOKEN');
  const did      = lsGet('ECOVACS_DID');
  const resource = lsGet('ECOVACS_RESOURCE') || 'res0';
  if (!uid || !token || !did) throw new Error('Ecovacs non connecté. Configurez les identifiants dans Réglages > Maison.');

  const payload = {
    auth:    { token, userid: uid, with: 'users', nick: '' },
    cmdName: cmd,
    did,
    mid:     did,
    payload: params,
    resource,
    td:      'q',
    toType:  'p',
    payloadType: 'j',
  };

  const res = await fetch(getProxyUrl(BASE_IOT), {
    method:  'POST',
    headers: { 'Content-Type': 'application/json' },
    body:    JSON.stringify(payload),
    signal:  AbortSignal.timeout(10000),
  });
  if (!res.ok) throw new Error('Commande Ecovacs échouée : HTTP ' + res.status);
  return await res.json();
}

// Dispatcher des commandes — appelé par le LLM via tool_use
export async function ecovacs_control({ action }) {
  switch (action) {
    case 'clean':
      return ecovacs_send_cmd('clean', { act: 'start', type: 'auto', speed: 'standard', content: {} });
    case 'charge':
      return ecovacs_send_cmd('charge', { act: 'go' });
    case 'stop':
      return ecovacs_send_cmd('clean', { act: 'pause' });
    case 'status': {
      const [cleanState, chargeState] = await Promise.all([
        ecovacs_send_cmd('getCleanState',  {}),
        ecovacs_send_cmd('getChargeState', {}),
      ]);
      return { cleanState, chargeState };
    }
    default:
      throw new Error('Action Ecovacs inconnue : ' + action);
  }
}

// Format OpenAI / Groq function calling
export const ECOVACS_TOOLS = [
  {
    type: 'function',
    function: {
      name:        'ecovacs_control',
      description: 'Contrôle l\'aspirateur robot Ecovacs Deebot (démarrer, pause, retour base, statut batterie)',
      parameters: {
        type: 'object',
        properties: {
          action: {
            type:        'string',
            enum:        ['clean', 'charge', 'stop', 'status'],
            description: 'clean = démarrer le nettoyage automatique, charge = retour à la base, stop = pause, status = état + batterie',
          },
        },
        required: ['action'],
      },
    },
  },
];
