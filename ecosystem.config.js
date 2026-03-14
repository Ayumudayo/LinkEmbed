module.exports = {
  apps : [{
    name   : "LinkEmbed-Bot",
    script : "./target/release/linkembed",
    cwd    : __dirname,
    autorestart: true,
    watch  : false,
    max_memory_restart: '1G',
    exec_mode: 'fork',
    interpreter: 'none'
  }]
}
