module.exports = {
  apps : [{
    name   : "LinkEmbed-Bot",
    script : "./build/linux-x64-release/LinkEmbed",
    cwd    : __dirname,
    autorestart: true,
    watch  : false,
    max_memory_restart: '1G',
    exec_mode: 'fork',
    interpreter: 'none'
  }]
}
