// The `info` argument of Vue's errorCaptured hook names where the error was
// thrown: "render function" in a development build, and in a production build
// the error-reference URL ending in "#runtime-1", 1 being Vue's
// ErrorCodes.RENDER_FUNCTION. The bare code is accepted too.
const RENDER_FUNCTION = 1
const RENDER_FUNCTION_DEV = 'render function'
const RENDER_FUNCTION_PROD = `https://vuejs.org/error-reference/#runtime-${RENDER_FUNCTION}`

/** Whether an errorCaptured `info` names a render function, as opposed to a handler, watcher or lifecycle hook. */
export function isRenderError(info: unknown): boolean {
  return info === RENDER_FUNCTION_DEV || info === RENDER_FUNCTION_PROD || info === RENDER_FUNCTION
}
