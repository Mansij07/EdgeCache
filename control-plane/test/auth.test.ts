import assert from 'node:assert';
import { test } from 'node:test';
import { requireAdmin } from '../src/middleware/auth';
import { Config } from '../src/config';

const cfg = { adminApiKey: 'secret' } as Config;

function mockReqRes(authHeader?: string) {
  const req: any = { header: (n: string) => (n.toLowerCase() === 'authorization' ? authHeader : undefined) };
  const res: any = {
    statusCode: 200,
    body: undefined,
    status(c: number) {
      this.statusCode = c;
      return this;
    },
    json(b: unknown) {
      this.body = b;
      return this;
    },
  };
  return { req, res };
}

test('rejects missing token', () => {
  const { req, res } = mockReqRes(undefined);
  let nextCalled = false;
  requireAdmin(cfg)(req, res, () => (nextCalled = true));
  assert.equal(res.statusCode, 401);
  assert.equal(nextCalled, false);
});

test('rejects wrong token', () => {
  const { req, res } = mockReqRes('Bearer nope');
  let nextCalled = false;
  requireAdmin(cfg)(req, res, () => (nextCalled = true));
  assert.equal(res.statusCode, 401);
  assert.equal(nextCalled, false);
});

test('accepts correct bearer token', () => {
  const { req, res } = mockReqRes('Bearer secret');
  let nextCalled = false;
  requireAdmin(cfg)(req, res, () => (nextCalled = true));
  assert.equal(nextCalled, true);
});
