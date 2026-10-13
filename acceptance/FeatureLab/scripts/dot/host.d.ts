/** Explicit native transport; available only when services are registered.
 * 显式原生传输，仅注册服务时可用。 */
declare module 'dot:host' {
  export function invoke(method: string, arguments_: readonly unknown[]): unknown;
}
