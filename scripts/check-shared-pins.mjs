// Fails when a frontend package depends on @lostpointer/* by range, tag, file: or git ref.
// Shared packages are released from LostPointer/web-platform; consumers adopt a version explicitly.
import { existsSync, readdirSync, readFileSync } from 'node:fs'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'

const root = fileURLToPath(new URL('..', import.meta.url))
const exact = /^\d+\.\d+\.\d+(-[0-9A-Za-z.-]+)?$/
const sections = ['dependencies', 'devDependencies', 'optionalDependencies']

const manifests = ['package.json']
const common = join('projects', 'frontend', 'common', 'package.json')
if (existsSync(join(root, common))) manifests.push(common)
const appsDir = join('projects', 'frontend', 'apps')
for (const entry of readdirSync(join(root, appsDir), { withFileTypes: true })) {
  const manifest = join(appsDir, entry.name, 'package.json')
  if (entry.isDirectory() && existsSync(join(root, manifest))) manifests.push(manifest)
}

const problems = []
for (const manifest of manifests) {
  const pkg = JSON.parse(readFileSync(join(root, manifest), 'utf8'))
  for (const section of sections) {
    for (const [name, spec] of Object.entries(pkg[section] ?? {})) {
      if (name.startsWith('@lostpointer/') && !exact.test(spec)) {
        problems.push(`${manifest}: ${section}.${name} is "${spec}", expected an exact version like 1.2.3`)
      }
    }
  }
}

if (problems.length > 0) {
  console.error(problems.join('\n'))
  process.exit(1)
}
console.log(`@lostpointer/* pinned exactly in ${manifests.length} manifests`)
