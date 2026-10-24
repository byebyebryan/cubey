import { posix } from 'node:path';

/** Relative URLs preserve the same static build under any hosting prefix. */
export function url(target: string, current: string): string {
  if (/^(?:https?:|mailto:|#|\?)/.test(target)) return target;
  const match=target.match(/^([^?#]*)(.*)$/)!;
  const pathname=match[1].replace(/^\/+/, '');
  const from=current.replace(/^\/+|\/+$/g, '');
  let relative=posix.relative(from,pathname) || '.';
  if ((!pathname || pathname.endsWith('/')) && !relative.endsWith('/')) relative+='/';
  return relative+match[2];
}
