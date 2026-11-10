import { VeHttpClient } from '../dist/index.js';

const client = new VeHttpClient(process.argv[2] || 'http://127.0.0.1:12000', {
  timeout: 5000,
  maxResponseBytes: 4 * 1024 * 1024,
});
const path = process.argv[3] || '';

try {
  const [, childrenOperation] = await Promise.all([
    client.describe('meta'),
    client.describe('children'),
  ]);
  const properties = childrenOperation.input_schema?.properties;
  if (
    !properties ||
    typeof properties !== 'object' ||
    !('meta' in properties) ||
    !('value' in properties)
  )
    throw new Error('The service does not advertise the key/meta children contract');

  const metadata = await client.meta(path);
  if (
    typeof metadata.type !== 'string' ||
    !Number.isSafeInteger(metadata.count) ||
    metadata.count < 0
  )
    throw new Error('Invalid meta response');

  async function readPage(offset) {
    const page = await client.children(path, { offset, limit: 8, meta: true });
    const children = page.children === null ? [] : page.children;
    if (!Array.isArray(children)) throw new Error('Invalid children response');
    return children.map((child, index) => {
      if (
        typeof child.key !== 'string' ||
        typeof child.type !== 'string' ||
        !Number.isSafeInteger(child.count) ||
        child.count < 0
      )
        throw new Error('Invalid child metadata');
      return { index: offset + index, key: child.key, type: child.type, count: child.count };
    });
  }

  const first = await readPage(0);
  const last = metadata.count > 8 ? await readPage(metadata.count - 8) : first;
  console.log(JSON.stringify({ path, metadata, first, last }, null, 2));
} catch (error) {
  console.error(error.message);
  process.exitCode = 1;
}
