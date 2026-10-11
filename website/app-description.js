// Copy-to-clipboard for every button with a data-copy attribute.
document.querySelectorAll('button[data-copy]').forEach(function (button) {
  button.addEventListener('click', function () {
    var target = document.getElementById(button.getAttribute('data-copy'));
    if (!target || !navigator.clipboard) {
      return;
    }
    var original = button.textContent;
    navigator.clipboard.writeText(target.textContent.trim()).then(function () {
      button.textContent = 'Αντιγράφηκε';
      setTimeout(function () { button.textContent = original; }, 1500);
    });
  });
});
