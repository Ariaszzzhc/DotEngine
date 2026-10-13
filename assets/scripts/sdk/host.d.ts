/** Explicit native transport; unregistered methods fail with contextual native errors.
 * 显式原生传输；未注册方法以带上下文的原生错误失败。 */
declare module 'dot:host' {
  export function invoke(method: string, arguments_: readonly unknown[]): unknown;
}
