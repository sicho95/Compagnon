import { lsGet, lsSet } from '../storage/agents-db.js';

const LWA_AUTH_URL  = 'https://www.amazon.com/ap/oa';
const LWA_TOKEN_URL = 'https://api.amazon.com/auth/o2/token';
const ALEXA_API_URL = 'https://api.amazonalexa.com/v3/events';

function redirectUri() {
  return window.location.origin + window.location.pathname.replace(/\/$/, '');
}

function proxyUrl(target) {
  const base = (lsGet('SEARCH_PROXY_URL') || 'https://proxy.sicho95.workers.dev/').replace(/\/$/, '');
  return base + '/' + target;
}

// Construire l'URL OAuth LWA (Login with Amazon) pour Alexa Smart Home
export function alexa_auth_url() {
  const clientId = lsGet('ALEXA_CLIENT_ID');
  if (!clientId) throw new Error('ALEXA_CLIENT_ID non configuré dans Réglages.');
  const params = new URLSearchParams({
    client_id:     clientId,
    scope:         'alexa::smarthome',
    response_type: 'code',
    redirect_uri:  redirectUri(),
    state:         'nestor-alexa-' + Date.now(),
  });
  return LWA_AUTH_URL + '?' + params.toString();
}

// Échanger le code OAuth contre access_token + refresh_token
export async function alexa_exchange_code(code) {
  const clientId     = lsGet('ALEXA_CLIENT_ID');
  const clientSecret = lsGet('ALEXA_CLIENT_SECRET');
  if (!clientId || !clientSecret) throw new Error('ALEXA_CLIENT_ID / ALEXA_CLIENT_SECRET manquants dans Réglages.');

  const res = await fetch(LWA_TOKEN_URL, {
    method:  'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body:    new URLSearchParams({
      grant_type:    'authorization_code',
      code,
      redirect_uri:  redirectUri(),
      client_id:     clientId,
      client_secret: clientSecret,
    }),
  });
  if (!res.ok) throw new Error('Échange OAuth Alexa échoué : HTTP ' + res.status);
  const data = await res.json();
  lsSet('ALEXA_ACCESS_TOKEN',  data.access_token  || '');
  lsSet('ALEXA_REFRESH_TOKEN', data.refresh_token || '');
  return data;
}

// Rafraîchir l'access token
export async function alexa_refresh_token() {
  const clientId     = lsGet('ALEXA_CLIENT_ID');
  const clientSecret = lsGet('ALEXA_CLIENT_SECRET');
  const refreshToken = lsGet('ALEXA_REFRESH_TOKEN');
  if (!refreshToken) throw new Error('Aucun refresh token Alexa — reconnectez-vous via Réglages.');

  const res = await fetch(LWA_TOKEN_URL, {
    method:  'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body:    new URLSearchParams({
      grant_type:    'refresh_token',
      refresh_token: refreshToken,
      client_id:     clientId,
      client_secret: clientSecret,
    }),
  });
  if (!res.ok) throw new Error('Refresh Alexa échoué : HTTP ' + res.status);
  const data = await res.json();
  lsSet('ALEXA_ACCESS_TOKEN', data.access_token || '');
  if (data.refresh_token) lsSet('ALEXA_REFRESH_TOKEN', data.refresh_token);
  return data.access_token;
}

// Envoyer une directive Alexa Smart Home (avec retry sur 401)
async function sendDirective(namespace, name, endpointId, payload = {}, isRetry = false) {
  const token = lsGet('ALEXA_ACCESS_TOKEN');
  if (!token) throw new Error('Non connecté à Alexa. Configurez OAuth dans Réglages > Maison.');

  const directive = {
    event: {
      header: {
        namespace,
        name,
        messageId:      crypto.randomUUID(),
        payloadVersion: '3',
      },
      endpoint: { endpointId },
      payload,
    },
  };

  const res = await fetch(proxyUrl(ALEXA_API_URL), {
    method:  'POST',
    headers: { 'Content-Type': 'application/json', Authorization: 'Bearer ' + token },
    body:    JSON.stringify(directive),
    signal:  AbortSignal.timeout(10000),
  });

  if (res.status === 401 && !isRetry) {
    await alexa_refresh_token();
    return sendDirective(namespace, name, endpointId, payload, true);
  }
  if (!res.ok) throw new Error('Erreur Alexa API : HTTP ' + res.status);
  return { success: true, action: name, device: endpointId };
}

// Contrôler un appareil Alexa — appelé par le LLM via tool_use
export async function alexa_control({ device_name, action, brightness, color }) {
  // Résolution de l'endpointId : clé nommée ALEXA_DEVICE_<NOM> ou le nom directement
  const key      = 'ALEXA_DEVICE_' + device_name.toUpperCase().replace(/\s+/g, '_');
  const endpoint = lsGet(key) || device_name;

  switch (action) {
    case 'TurnOn':
      return sendDirective('Alexa.PowerController', 'TurnOn', endpoint);
    case 'TurnOff':
      return sendDirective('Alexa.PowerController', 'TurnOff', endpoint);
    case 'SetBrightness':
      return sendDirective('Alexa.BrightnessController', 'SetBrightness', endpoint, {
        brightness: Math.max(0, Math.min(100, brightness ?? 100)),
      });
    case 'SetColor': {
      const h = color?.hue        ?? 0;
      const s = color?.saturation ?? 1.0;
      const b = color?.brightness ?? 1.0;
      return sendDirective('Alexa.ColorController', 'SetColor', endpoint, {
        color: { hue: h, saturation: s, brightness: b },
      });
    }
    default:
      throw new Error('Action Alexa non reconnue : ' + action);
  }
}

// Format OpenAI / Groq function calling
export const ALEXA_TOOLS = [
  {
    type: 'function',
    function: {
      name:        'alexa_control',
      description: 'Contrôle un appareil connecté Alexa Smart Home (ampoule, prise connectée, etc.)',
      parameters: {
        type: 'object',
        properties: {
          device_name: {
            type:        'string',
            description: 'Nom de l\'appareil tel qu\'il apparaît dans l\'app Alexa (ex: "Lampe salon", "Prise bureau")',
          },
          action: {
            type:        'string',
            enum:        ['TurnOn', 'TurnOff', 'SetBrightness', 'SetColor'],
            description: 'Action à effectuer',
          },
          brightness: {
            type:        'number',
            description: 'Luminosité de 0 à 100 — requis pour SetBrightness',
          },
          color: {
            type:        'object',
            description: 'Couleur HSB — requis pour SetColor',
            properties: {
              hue:        { type: 'number', description: 'Teinte 0-360' },
              saturation: { type: 'number', description: 'Saturation 0.0-1.0' },
              brightness: { type: 'number', description: 'Luminosité 0.0-1.0' },
            },
          },
        },
        required: ['device_name', 'action'],
      },
    },
  },
];
